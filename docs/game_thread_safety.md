# Game Thread Safety

How script threads safely access game memory in d2bsng.

## Architecture

```
Game thread                     Script threads (one per script)
-----------                     -----------------------------
Runs the game frame loop        Each has own V8 isolate
Modifies game state             Reads game state via handles
Fires events to scripts         Processes events during delay()
Holds GameWriteLock per frame   Holds GameReadLock per resolve
```

Scripts and the game thread run concurrently. Game memory can be modified by the game thread at any time. The locking system prevents scripts from reading inconsistent or freed game data.

## Lock Primitives (GameLock.h)

### GameReadLock - script threads (shared, recursive)

Acquired automatically inside every `ResolvePtr()`. Multiple scripts hold shared locks concurrently - scripts never block each other. Recursive via thread-local counter: nested resolves (e.g., Room resolves, which resolves Level) are free (~1ns counter increment).

```cpp
// Automatic - you don't write this, ResolvePtr() does it
GameReadLock guard;
auto* ptr = /* walk game data structure */;
// ~guard releases when ResolvePtr returns
```

### GameWriteLock - game thread (exclusive)

The game thread holds `GameWriteLock` continuously across the frame body. `GameLoop::OnSleep` (dispatched from the per-version Sleep hook) runs per-frame framework work - `InvalidateHandles`, snapshot, chicken, state-event synthesis, drawable flush, script lifecycle - under the held lock. It then enters a deadline-based drain loop that releases and reacquires the lock per `idleSleepInterval` slice (default 10ms), draining `GameThread::Execute` tasks while script readers can acquire `GameReadLock`. When the deadline elapses, the lock is reacquired before returning to the game's frame work.

**LOCK-1**: `GameReadLock` is a no-op when the current thread already holds `GameWriteLock` - game-thread code inside the frame body can call `ResolvePtr()` freely without deadlocking on itself.

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

### Bridge::Lock() - explicit batched reads

For V8 callbacks that make multiple game reads requiring a consistent view. Returns a `GameReadLock` that covers the entire callback scope. Inner `ResolvePtr()` calls are recursive re-entries (free).

```cpp
Method(isolate, proto, "getItem", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto lock = d2bs::game::Bridge::Lock();  // Hold for entire inventory iteration
    // ... iterate inventory, each item.Name()/item.ClassId() is a recursive lock (free) ...
});
```

**When to use Bridge::Lock():**
- Multi-step game traversals (Player -> GetRoom -> GetLevel -> FirstRoom)
- Building a result from several reads that must agree with each other

The composed walks in `Finders.h` take it themselves - see "Which Callbacks Need
Bridge::Lock()" below - so a binding that only calls one of those needs nothing of its own.

**When NOT needed:**
- Simple property getters (single ResolvePtr per access)
- Single game method calls that return copied data (GetExits, GetCollision)

## HandleCache (HandleCache.h)

Each identity-based handle caches its resolved game pointer per-frame. First resolve walks the game data structure (~O(n)). Subsequent resolves in the same frame return the cached pointer (~O(1)).

`InvalidateHandles()` increments `frameGeneration`, invalidating all caches. Called inside `GameWriteLock` so no readers are active during invalidation.

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

Used inside `GameThread::Execute()`. Fully unwinds the recursive read lock depth, releases the shared lock, then restores on destruction.

## Which Callbacks Need Bridge::Lock()

The composed walks in `Finders.h` take the lock themselves, for the whole walk - `Matches()`
re-resolves the candidate for each field it tests, so without it a sweep pays a real
acquire/release per field per candidate. Bindings that call only these need nothing of
their own; an outer `Bridge::Lock()` is still free (recursive re-entry) and several keep one
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
(`getPresetUnit()`, `getPresetUnits()`) already hold one across the whole callback.

Simple property getters and single-method calls returning copies do NOT need
`Bridge::Lock()` - `ResolvePtr()` locks per resolve.

Note that several bindings hold the lock across V8 allocation (`getItem`, `getItems`,
`getPresetUnit`, `getPresetUnits` all construct wrappers under it). That is a latent stall:
an allocation can trigger a GC whose weak callbacks run native destructors
(`closesocket`, `sqlite3_close_v2`, `fclose`), and the game thread waits on the write lock
for the duration. Worth avoiding in new code; the existing sites are not yet fixed.
