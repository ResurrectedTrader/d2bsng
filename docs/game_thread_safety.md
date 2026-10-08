# Game Thread Safety

How script threads safely access game memory in d2bsng.

## Architecture

```
Game thread                     Script threads (one per script)
-----------                     -----------------------------
Runs the game frame loop        Each has own V8 isolate
Modifies game state             Reads game state via handles
Fires events to scripts         Processes events during delay()
Holds GameWriteLock per frame   Holds GameReadLock per accessor
```

Scripts and the game thread run concurrently. Game memory can be modified by the game thread at any time. The locking system prevents scripts from reading inconsistent or freed game data.

## Lock Primitives (GameLock.h)

### GameReadLock - script threads (shared, recursive)

Multiple scripts hold shared locks concurrently - scripts never block each other. Recursive via thread-local counter: nested acquisitions (a callback's `GameReadLock`, then each accessor it calls) are free (~1ns counter increment).

A resolved game pointer is only good while a read lock is held. The moment the lock drops, the game thread can take the write lock and free or relink the structure. So the lock is taken by whoever dereferences, not by the resolve: `ResolvePtr()` takes none of its own, and asserts (debug builds) that the calling thread already holds the game lock (`IsGameLockHeld()`: a read lock, or the game thread's write lock).

**Rule: a backend accessor reads through `Resolved<T>`.** `Resolved<T>` (`HandleCache.h`) is a move-only RAII value that takes a `GameReadLock`, then resolves the handle through the unchanged `ResolvePtr()` / `HandleCache` path, and holds the lock for its lifetime - so the resolve and every dereference happen under one lock. Handles produce it with the private `Resolve<T>()`, where `T` is the backend's own struct type; the contract never names it:

```cpp
uint32_t Unit::ItemLevel() const {
    const auto u = Resolve<D2UnitStrc>();   // lock, then resolve
    if (!u || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->dwItemLevel;       // still under the lock
}                                           // lock released here
```

Use `->` / `*` for the struct, and pass `u` itself where a game function wants the raw pointer: a named `Resolved<T>` converts implicitly to `T*`, which also serves the null test (`if (!u)`). The conversion is deleted on a temporary (`T* p = Resolve<T>();` does not compile), because the lock would be gone before the pointer is used. A static factory that reads game memory directly (`Unit::CursorItem`, `Control::GetFirst`, `Level::Get`, ...) and a validity test (`operator bool`) hold an explicit `GameReadLock guard;` instead.

**A lock-releasing wait invalidates a `Resolved<T>`; re-resolve after it.** `GameThread::Execute`, `PollUntil` and anything else built on `GameReadLockReleaser` hand the thread's read lock back while they wait (the game thread needs the write lock to run frames and drain posted work), so every pointer resolved before the wait may be stale after it. A method that waits keeps its reads in an inner scope that closes before the wait and copies out the plain values it needs (`Unit::Move`, `Unit::Interact`, `Unit::EquipItem`, `Control::Click`); work posted through `GameThread::Execute` captures the handle, not its pointer, and resolves it again inside the task on the game thread (`Control::SetText`, `Room::Reveal`). A plain sleep must not happen under any read lock at all - it releases nothing and stalls the game thread.

Debug builds check this: each releaser that actually gives a lock back (`GameReadLockReleaser` releasing a held read lock, `GameWriteLockReleaser` yielding the write lock) bumps a thread-local release epoch (`GameReadLock::ReleaseEpoch()`); `Resolved<T>` snapshots it on creation and asserts it unchanged in `operator->`, `operator*` and the `T*` conversion. Release builds compile the check out.

### GameWriteLock - game thread (exclusive)

The game thread holds `GameWriteLock` continuously across the frame body. `GameLoop::OnSleep` (dispatched from the per-version Sleep hook) runs per-frame framework work - `InvalidateHandles`, snapshot, chicken, state-event synthesis, drawable flush, script lifecycle - under the held lock. It then enters a deadline-based drain loop that releases and reacquires the lock per `idleSleepInterval` slice (default 10ms), draining `GameThread::Execute` tasks while script readers can acquire `GameReadLock`. When the deadline elapses, the lock is reacquired before returning to the game's frame work.

**LOCK-1**: `GameReadLock` is a no-op when the current thread already holds `GameWriteLock` - game-thread code inside the frame body can resolve handles freely without deadlocking on itself (and the write lock satisfies `ResolvePtr()`'s assert).

```cpp
// Per-frame, on the game thread:
void GameLoop::OnSleep(std::chrono::milliseconds duration) {
    // Body runs under the already-held GameWriteLock
    InvalidateHandles();
    TakeSnapshot(cur);
    // ... chicken, events, drawables, script lifecycle ...

    // Drain loop: release/reacquire per idleSleepInterval slice until deadline
    while (now < deadline) {
        GameWriteLock::Release();
        ::Sleep(idleSleepInterval);
        GameWriteLock::Acquire();
        GameThread::Drain();
    }
    // Lock left held on exit - next frame body starts under it
}
```

### Batched reads - one GameReadLock across a callback

There is one read lock. A V8 callback that makes multiple game reads requiring a consistent view holds a `GameReadLock` across its scope; the accessors' own `Resolved<T>` locks inside it are recursive re-entries (free).

```cpp
function::Register(isolate, global, "getControls", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
    std::vector<game::Control> controls;
    {
        game::GameReadLock lock;  // one consistent walk of the control list
        if (auto first = game::Control::GetFirst()) {
            // GetNext() yields an empty Control at the end of the list.
            for (auto c = *first; c; c = c.GetNext()) {
                controls.push_back(c);  // each accessor's own lock is a recursive re-entry (free)
            }
        }
    }
    // ... build the JS array from `controls`, outside the lock ...
});
```

**When to hold one:**
- Multi-step game traversals (Player -> GetRoom -> GetLevel -> FirstRoom)
- Building a result from several reads that must agree with each other
- Testing a handle that did not come from `Unwrap` (`Unit::Player()`, `Unit::InteractingNPC()`) and then reading it (`getMercHP()`, `getDistance()`, `getArea()`) - a wrapper's own handle gets this from `Unwrap`, see "Bindings" below

**Scope it tightly.** Take the lock right before the first game read - after argument validation and extraction, early returns and `WaitForGameReady` - and release it after the last one: copy what you need into locals inside a scope, then create V8 values, format, log or call back into scripts outside it. Reads that must agree with each other (a walk, or a unit and then its items) stay under one lock rather than several short ones.

The composed walks in `Finders.h` take it themselves - see "Which Callbacks Need
a GameReadLock" below - so a binding that only calls one of those needs nothing of its own.

**When NOT needed:**
- Single game method calls that return copied data (GetCollision) - the backend accessor holds the lock for its own read through `Resolved<T>` (see "GameReadLock" above) - and the navigation component's composed reads (`runtime::navigation::GetExits`), which take a `GameReadLock` themselves
- Reads through a wrapper's own handle: `Unwrap` already holds one (next section)

### Bindings - `Unwrap` returns a LockedHandle

Every accessor on a handle resolves it again, so a binding that tests a handle and then reads it takes two lock windows - `if (!*data)` in one and `data->GetStat(n)` in the next - and the game thread can free the object in between. The script then sees `0` or `""` where it should have seen `undefined`. Nothing unsafe happens (each read re-resolves under its own lock), but the answer is inconsistent.

So for the game handle natives - `game::Unit`, `Room`, `Level`, `Party`, `Control`, `StashTab`, named by the `GameHandle` concept (`api/core/LockedHandle.h`) - `ClassBase::Unwrap` returns a `LockedHandle<T>` rather than a `T*`. It takes a `GameReadLock` when it is created and holds it for its lifetime, and gives the handle through `->` / `*`, so the validity test and every read after it run against one game state. Its own `operator bool` is the pointer test (false when the receiver was not a wrapper of this class); `*data`'s bool is still the "does the game object exist" test. The accessors' own `Resolved<T>` locks nest inside it for free. Every other class - value natives such as `ExitInfo` and `PresetUnitInfo`, and the script-owned ones (File, SQLite, Socket, Script, the drawables, ...) - still gets its raw pointer; the choice is made from the type at compile time, and there is no unlocked `Unwrap` for a game handle.

```cpp
Property(isolate, inst, "hp", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
    const auto data = Unwrap(info.Holder());  // lock taken here ...
    if (!*data) {
        return;                               // undefined: the unit is gone
    }
    info.GetReturnValue().Set(data->Hp());    // ... so the unit is still there for this read
});                                           // ... and released here
```

While a `LockedHandle` is alive the game thread cannot take its write lock, so it follows the same rules as any `GameReadLock`, and a few more because it is taken on every property read:

- **Arguments first, then the guard.** A conversion of an untyped value can run script (`valueOf`, `toString`, a getter), so a binding converts every argument it needs before its `Unwrap`, then takes one guard and does its checks and reads under it. A conversion of an argument already gated by `IsString` / `IsNumber` / `IsUint32` runs no script and can stay where it is.
- **Never run script while holding it** - no callbacks, no `ExecuteEvents`, no calls into JS functions (the Unit iterator calls the array's iterator method only after the scope closes).
- **Never wait while holding it.** `WaitForGameReady` sleeps without releasing anything, so it runs before the `Unwrap`. A method that can wait on the game thread on some backend (`Unit::Move`, `Interact`, `TakeWaypoint`, `UseMenu`, `Shop`, `EquipItem`, `Description`; `Control::Click`, `SetText`, `SetState`, `SetCursorPos`, `Text`, `TextLines`; `Room::Reveal`; `StashTab::Click`, `MoveGold`; `ClickMapAt`, `ClickItem`, `ClickPartyMember`, `LeaveParty`, `MoveNPC`; `setSkill`'s bind loop) runs on a copy of the handle after the scope closes. `GameThread::Execute` would hand the lock back anyway, but the binding's reads before it would then no longer agree with the result, and a plain sleep would stall the game.
- **Build large results after it.** Scope the guard in a block, copy arrays and lists of handles out (stats, collision, items, rooms), and build the V8 arrays and instances after the block. A number or a single string is built directly under it.

```cpp
Method(isolate, proto, "getItems", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
    // WaitForGameReady first: it sleeps and must not hold the lock.
    std::vector<game::Unit> items;
    {
        const auto data = Unwrap(args.This());
        if (!*data) {
            return;
        }
        items = data->GetItems();
    }
    // ... CreateInstance for each item, outside the lock ...
});

Method(isolate, proto, "move", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
    game::Unit unit;
    {
        const auto data = Unwrap(args.This());
        if (!*data) {
            return;
        }
        unit = *data;
    }
    unit.Move(target);  // waits on the game thread; the copy re-resolves on its own
});
```

`const game::StashTab tab = *Unwrap(args.This());` is the one-line form of that copy: the temporary guard ends with the statement.

The same guard covers a handle taken from an argument after `IsInstance` (`clickItem(unit)`, `getDistance(unit)`, `room.unitInRoom(unit)`): `const auto unitData = JSUnit::Unwrap(obj); if (!unitData || !*unitData) ...`. Two guards at once (`checkCollision`) are two re-entries of the same lock.

One exception to "one game state per guard": the first resolve of a room or level the game has not built yet runs the level's init on the game thread, and hands the lock back while it does. The reads after it can then see a later frame than the ones before it. That is safe, because a guard holds identity handles, not raw pointers - every read re-resolves - but a binding cannot rely on its reads agreeing across such a resolve.

## HandleCache (HandleCache.h)

Each identity-based handle caches its resolved game pointer per-frame. First resolve walks the game data structure (~O(n)). Subsequent resolves in the same frame return the cached pointer (~O(1)).

`InvalidateHandles()` increments `frameGeneration`, invalidating all caches. Called inside `GameWriteLock` so no readers are active during invalidation.

`Resolved<T>` resolves through this same cache (`Resolve<T>()` calls `ResolvePtr()`); it only adds the lock lifetime, the typed pointer and the debug epoch check.

## GameThread (GameThread.h)

Some game functions must be called from the game thread (UI control manipulation, functions depending on thread-local storage).

```cpp
// From a script thread:
game::GameThread::Execute([&] {
    auto ctrl = game::Control::Find(/* login button */);
    if (ctrl) ctrl->Click();
});
// Blocks until game thread executes the function
// GameReadLock automatically released while waiting
```

`Execute()` releases the script's `GameReadLock` so the game thread can acquire `GameWriteLock` to drain the task queue.

## Per-frame events (`render`)

The `render` event is a signal, not a queued event. `GameLoop::OnDraw` (the backend's `onDraw` callback, once
per rendered frame, in and out of game) only increments a global frame counter (`events::FrameCounter`,
`components/events/FrameCounter.h`) before `Drawable::DrawAll`; it queues nothing and never waits on a script,
so a frame costs one atomic increment whether or not any script listens. Each pass of a script's event loop
(`Script::ExecuteEvents`, i.e. inside `delay()`) compares the counter with the frame the script last fired
`render` for and, if it moved and the script listens for `render`, fires the listeners once with the latest frame
number. No wake-up is
needed: the loop already polls every `IdleSleepIntervalMs`, the same latency as any cross-thread posted event. A
slow script just sees a bigger jump in `frame`; there is no backlog to drain.

Handlers run on the script's thread, so a frame is usually already being drawn by then: a drawable change made
in a `render` handler typically shows from the next frame. Drawable fields are atomics, so the handler needs no
game lock to change them; reading game state from it takes the usual `GameReadLock`, which waits for the game
thread's sleep window.

## Lock Releasers

### GameWriteLockReleaser

The game thread holds `GameWriteLock` during its frame. For blocking events (key/chat/packet), it must release the write lock so scripts can acquire read locks to process the event handler:

```
Game thread: [GameWriteLock held] -> fire event -> [release write lock] -> wait on promise
Script:      ... [acquire read lock] -> process handler -> resolve promise -> [release] ...
Game thread: [re-acquire write lock] -> check result -> continue
```

### GameReadLockReleaser

Used inside `GameThread::Execute()` and `PollUntil()`. Fully unwinds the recursive read lock depth, releases the shared lock, then restores on destruction. Releasing a held lock bumps the release epoch, so a `Resolved<T>` used after it trips its debug assert.

## Which Callbacks Need a GameReadLock

The composed walks in `Finders.h` take the lock themselves, for the whole walk - `Matches()`
re-resolves the candidate for each field it tests, so without it a sweep pays a real
acquire/release per field per candidate. Bindings that call only these need nothing of
their own; an outer `GameReadLock` is still free (recursive re-entry) and several keep one
because they do more than the walk.

| Walk | Reached from |
|---|---|
| `Unit::FindFirst` / `FindNext` | `getUnit()`, `unit.getNext()` |
| `Unit::FindFirstInventoryItem` / `FindNextInventoryItem` | `getItem()`, `item.getNext()` |
| `Unit::FindMerc` | `getMerc()`, `getMercHP()` |
| `Unit::GetItems` | `getItems()` |
| `Level::FindRoomAt` | `getRoom()`, `getCollision()` |
| `Party::FindById` / `FindByName` | `getParty()` |
| `Control::Find` | `getControl()` |

`Level::GetPresetUnits` / `FindFirstPresetUnit` do not take it themselves - their bindings
(`getPresetUnit()`, `getPresetUnits()`) hold one across the level lookup and the walk.

Single-method calls returning copies do NOT need a lock of their own - each backend
accessor holds the read lock across its own resolve and read. A property getter or method
on a game handle wrapper gets one from `Unwrap` (see "Bindings" above) and needs no other.

Don't hold the lock across V8 allocation or other script-facing work: an allocation can
trigger a GC whose weak callbacks run native destructors (`closesocket`,
`sqlite3_close_v2`, `fclose`), and reading a script object's properties can run its getters,
while the game thread waits on the write lock for the duration. Copy the game data out
under the lock, then build the JS values after it (as `getControls()`, `getPresetUnits()`
and `getRoom()` do). A number or a single string is cheap enough to build under it, as the
bindings above do.
