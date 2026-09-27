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

**Scope it tightly.** Take the lock right before the first game read - after argument validation and extraction, early returns and `WaitForGameReady` - and release it after the last one: copy what you need into locals inside a scope, then create V8 values, format, log or call back into scripts outside it. Reads that must agree with each other (a walk, or a unit and then its items) stay under one lock rather than several short ones.

The composed walks in `Finders.h` take it themselves - see "Which Callbacks Need
a GameReadLock" below - so a binding that only calls one of those needs nothing of its own.

**When NOT needed:**
- Simple property getters and single game method calls that return copied data (GetCollision) - the backend accessor holds the lock for its own read through `Resolved<T>` (see "GameReadLock" above) - and the navigation component's composed reads (`runtime::navigation::GetExits`), which take a `GameReadLock` themselves

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

Simple property getters and single-method calls returning copies do NOT need
a lock of their own - each backend accessor holds the read lock across its own resolve and read.

Don't hold the lock across V8 allocation or other script-facing work: an allocation can
trigger a GC whose weak callbacks run native destructors (`closesocket`,
`sqlite3_close_v2`, `fclose`), and reading a script object's properties can run its getters,
while the game thread waits on the write lock for the duration. Copy the game data out
under the lock, then build the JS values after it (as `getControls()`, `getPresetUnits()`
and `getRoom()` do).
