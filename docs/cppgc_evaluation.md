# CppHeap (Oilpan) evaluation

Whether d2bsng should move its V8 wrapper objects from manual `new`/weak-callback ownership onto V8's C++ garbage collector.

Verified against the V8 checkout at `../v8` (14.0.264), which is the same major version the shipped `dependencies/v8` monolith was built from. Claims below cite files and lines; anything not directly verified is marked **(inference)**.

## What CppHeap is

`cppgc` (Oilpan) is a trace-based mark-and-sweep collector for C++ objects. Attached to an isolate as a `v8::CppHeap`, it forms a *unified heap*: a single GC traces the JS and C++ object graphs together, so a C++ object held only by a JS wrapper (and vice versa) is collected in one cycle rather than through V8's two-pass weak callbacks.

The embedder contract:

- Types derive from `cppgc::GarbageCollected<T>` and provide `void Trace(cppgc::Visitor*) const` (`include/cppgc/garbage-collected.h:52`).
- `operator new` and `operator delete` are **deleted**; allocation is only via `MakeGarbageCollected()` (`garbage-collected.h:58-68`). Manual deletion is a fatal error under `V8_ENABLE_CHECKS`.
- On-heap references must be `Member<T>` / `WeakMember<T>`; off-heap roots are `Persistent<T>`. Raw pointers to on-heap objects are unobservable edges and cause UAF — the README is explicit that only on-stack raw pointers are permitted.
- JS→C++ uses `v8::Object::Wrap` / `Unwrap` against `v8::Object::Wrappable`, which is itself a `cppgc::GarbageCollected` (`include/v8-object.h:564`). C++→JS uses `v8::TracedReference`.

### The threading model is the headline

From `include/cppgc/README.md`:

> Oilpan features thread-local garbage collection and assumes heaps are not shared among threads. In other words, objects are accessed and ultimately reclaimed by the garbage collector on the same thread that allocates them.

and, on sweeping:

> Even with concurrent sweeping, destructors are guaranteed to run on the thread the object has been allocated on to preserve C++ semantics.

This is a real guarantee, not a doc aspiration. The sweeper distinguishes `MutatorThreadSweepingMode::{kOnlyFinalizers, kAll}` (`src/heap/cppgc/sweeper.cc:58-61`) so background threads reclaim memory while finalizers stay on the mutator thread. Prefinalizers carry the same promise — "invoked on the same thread as the object was created on" (`include/cppgc/prefinalizer.h`).

Affinity is enforced, not merely documented: `HeapBase::CurrentThreadIsHeapThread()` compares against a recorded thread id (`src/heap/cppgc/heap-base.cc:351-353`), and `Member` reference checking asserts on it (`src/heap/cppgc/pointer-policies.cc:65`).

### Termination actually destroys things

`HeapBase::Terminate()` (`src/heap/cppgc/heap-base.cc:224-296`) clears every root region, then loops up to `kMaxTerminationGCs = 20` cycles of prefinalizers + atomic sweep until no persistent nodes remain, and finishes with:

```cpp
CHECK_EQ(0u, strong_persistent_region_.NodesInUse());
CHECK_EQ(0u, weak_persistent_region_.NodesInUse());
```

`CppHeap::Terminate` requires prior detach — `CHECK(!isolate_)` (`src/heap/cppgc-js/cpp-heap.cc:542-551`) — and the public `Terminate()` is deprecated in favour of it being "automatically called in the CppHeap destructor" (`include/v8-cppgc.h:91`).

This is the sharpest contrast with what d2bsng has today. As established previously: V8's `Isolate::Deinit` never drains queued second-pass weak callbacks and `GlobalHandles::~GlobalHandles()` is defaulted, so a wrapper still queued at `Dispose` is silently dropped and its native leaks. Under cppgc, teardown *runs the destructors*.

The cost of that guarantee is the `CHECK`: a leaked root is a **process abort**, where d2bsng currently logs `Instance leak: n X instance(s) not freed` (`src/frontends/js/components/script/Script.cpp:349`) and carries on. For a DLL injected into a game the user is playing, a logged line is a materially better failure mode than an abort.

## How d2bsng's model works today

`V8ClassBase<Derived, NativeType>` (`src/frontends/js/api/core/V8Class.h`) wraps a raw `NativeType*` in internal field 0, with a per-class type tag in field 1 (`V8Class.h:163-164`). The tag is the address of a `constinit` static seeded from the class name to defeat `/OPT:ICF` COMDAT folding (`V8Class.h:58`). `Unwrap` is a pointer compare against that address (`V8Class.h:153`).

Lifetime is a two-pass weak callback: first pass resets the handle, second pass does `delete d->native` and decrements the instance tracker (`V8Class.h:81-93`). The tracker exists as leak-detection scaffolding, read in the isolate `shared_ptr` deleter (`Script.cpp:342-351`).

Wrapped types, from `grep`:

- **20** direct `V8ClassBase` instantiations plus **5** `JSDrawableBase` subclasses.
- Resource holders: `SQLiteData` (`sqlite3*`), `DBStatementData` (`sqlite3_stmt*` + `v8::Global<v8::Object>`), `SocketData`, `FileData`, `SandboxData` (`v8::Global<v8::Context>`).
- Value types: `game::Unit`, `game::Level`, `game::Room`, `game::Party`, `game::Control` and friends — ids and cursor state, no external resource (`src/contract/game/Unit.h:20-32`).

Integration surface: **270** `Unwrap(` call sites, 29 `CreateInstance(`, 29 `GetTemplate(`, 25 `ClearCache(`, 11 `IsInstance(`, 5 `InitInstance(`, across 17,011 lines under `api/`.

## Problem-by-problem assessment

### (a) Natives leaking when weak callbacks are queued at Dispose

**Fixed, and properly.** This is cppgc's strongest argument. `HeapBase::Terminate()` guarantees destruction where V8's weak callbacks guarantee nothing. It would also delete the whole "drain before disposal" dance in `TeardownIsolate` — the `RequestGarbageCollection()` + `LowMemoryNotification()` pair whose correctness currently rests on `RequestGarbageCollectionForTesting` passing `kGCCallbackFlagForced`, which in turn needs `--expose-gc`.

Worth weighing honestly: this is a leak *at script teardown*, bounded by one script's live wrapper set, in a process that is itself long-lived but whose scripts come and go. It has not been measured as a problem.

### (b) Destructor thread affinity for the `v8::Global`-holding types

**Fixed structurally.** `DBStatementData::cachedRow` and `SandboxData::context` would be destroyed on their allocating thread by construction, rather than by the arrangement PR #14 arrived at.

But this problem was already established to be unreachable in the current configuration, and PR #14 made the counter correct regardless. cppgc would be replacing a working five-line fix with a heap.

### (c) The instance tracker as leak scaffolding

**Partially addressed, and arguably made worse.** cppgc's leak signal is `CHECK_EQ(0u, ...NodesInUse())` — an abort inside a game process. The tracker's logged line is the better ergonomics here. Deleting ~190 lines of tracker is real, but the replacement diagnostic is worse for this deployment.

### Not addressed at all

Nothing about the isolate `shared_ptr` ownership question changes. `CppHeap` is attached to an isolate and detached before termination; who owns the isolate and which thread runs `Dispose` remains exactly as it is.

## What gets worse

### Drawable is the blocker

`Drawable` is `std::enable_shared_from_this<Drawable>` (`src/frontends/js/components/drawing/Drawable.h:19`), holds lock-free atomics chosen for concurrent access, and is read from the **game thread** through static entry points — `DrawAll`, `OnClick`, `OnMouseMove`. `Script::GetDrawables()` hands the game thread `shared_ptr` copies.

None of this survives contact with Oilpan:

- cppgc objects cannot be `shared_ptr`-owned; `operator new`/`delete` are deleted and the heap owns the object.
- "Oilpan heaps may generally not be accessed from different threads unless otherwise noted" (README). The game thread reading a script-thread-heap object is outside the model.
- `CrossThreadPersistent` makes *holding a reference* legal, not *touching the object* while the owning thread may be sweeping.

Making the 5 drawing classes work would mean redesigning the game-thread rendering path — the single largest piece of work here, and it buys nothing, because `Drawable`'s lifetime is already explicitly managed by `AddDrawable`/`RemoveDrawable` rather than by GC.

### Destruction becomes non-deterministic for RAII resources

`SQLiteData` and `DBStatementData` are *literally* the pattern the cppgc README warns about:

> imagine a case where X is a client of Y, and Y holds a list of clients. If the code relies on X's destructor removing X from the list, there is a risk that Y iterates the list and calls some method of X which may touch other on-heap objects.

`DBStatementData` holds `SQLiteData* parent` and its `Finalize()` removes itself from the parent; `SQLiteData` holds `std::set<DBStatementData*> statements` and its `Close()` closes them all. Destruction order under cppgc is unspecified and destructors may not touch other on-heap objects, so **both** types need prefinalizers, and the mutual pointers need to become `Member`/`WeakMember`.

Beyond correctness there is a behavioural change: a script that drops its last reference to a file or socket currently closes the handle at that moment. Under GC it closes at some later collection. For a bot holding OS handles across long sessions that is a regression, and the fix is explicit `close()` discipline in JS — which is a user-visible API change.

The README also warns prefinalizers are "heavy because the thread needs to scan all pre-finalizers at each sweeping phase" and "should be avoided" on frequently created objects. `JSUnit` is created constantly by `getUnit`/`getNext`; it needs no prefinalizer (it holds no resource), so this is survivable — but it constrains the design.

### No type-safety win on x86

`v8::Object::Unwrap` type tagging is implemented in `ReadCppHeapPointerField` (`include/v8-sandbox.h:142-184`) and the tag check lives entirely inside `#ifdef V8_COMPRESS_POINTERS`. The `#else` branch is a bare `ReadRawField<Address>` with no check at all.

Pointer compression requires 64-bit, and d2bsng builds x86 — `js.vcxproj` defines no `V8_COMPRESS_POINTERS` **(inference: from the absence of the define and the 32-bit target; I did not disassemble the monolith to confirm how it was configured)**. So the `CppHeapPointerTag` range machinery, which is the nicest part of the modern wrapper API and would otherwise replace the hand-rolled COMDAT-folding tag trick, is inert here. `typeTag_` and the `Unwrap` pointer compare stay exactly as they are.

### Debuggability

A wrapper's lifetime becomes a function of GC timing rather than of a call site. Today `delete d->native` is one line in one place with a stack trace. That is a real loss for a codebase whose native side straddles a game process.

## Build cost

Cheaper than expected. The shipped monolith already contains cppgc:

```
?IsInitialized@cppgc@@YA   ?InitializeProcess@cppgc@@YAXPAVPageAllocator@v
?ShutdownProcess@cppgc@@YAXXZ
```

and the embedder entry points are present — `Create@CppHeap` (4 hits), `AttachCppHeap` (5), `Terminate@CppHeap` (3), `MakeGarbageCollectedTrait` (14) in `dependencies/v8/libs/x86-release/v8_monolith.lib`. Headers ship under `dependencies/v8/include/v8/cppgc/`.

So **no V8 rebuild is required**. This removes what would otherwise have been the dominant cost.

## Integration sketch

It can be done incrementally — cppgc and manual wrappers coexist, since `V8ClassBase` could keep its current path for some `NativeType`s and use `MakeGarbageCollected` for others. A plausible order:

1. Create one `CppHeap` per script isolate in `SetupIsolate`, attach via `CppHeapCreateParams`, detach in `TeardownIsolate` before `Dispose`. Isolate creation moves to the `CppHeapCreateParams` overload.
2. Convert the value types first (`game::Unit`, `Level`, `Room`, `Party`, `Control`, `Exit`, `PresetUnit`) — no resources, empty or near-empty `Trace`, no prefinalizers. Roughly 8 types.
3. Convert the resource holders with prefinalizers and `Member`-ised parent/child links: `SQLiteData`, `DBStatementData`, `SocketData`, `FileData`, `SandboxData`. Roughly 5 types, and the fiddliest.
4. Leave the 5 drawing classes on the current path indefinitely, or redesign the game-thread path first.

`Unwrap`'s 270 call sites need not all change if `V8ClassBase::Unwrap` keeps its signature and switches implementation internally — **(inference: I did not attempt the refactor; some call sites may depend on the raw-pointer return in ways that a `Member`-based API would break.)**

## Complexity delta

**Deleted**
- `MakeWeak` and the two-pass callback (~30 lines, `V8Class.h:71-95`).
- The pre-drain in `TeardownIsolate` and its dependence on `--expose-gc`.
- Potentially `V8InstanceTracker.h` (~190 lines) and the leak check in `Script.cpp`.

**Added**
- `Trace(cppgc::Visitor*)` on ~20 types, plus `Member`/`WeakMember` conversion of any inter-object pointers.
- Prefinalizers on ~5 resource types, with their parent/child cleanup restructured off destructors.
- CppHeap lifecycle in `Script.cpp`, and detach-before-dispose ordering added to a teardown path that is already the most delicate code in the file.
- A `Drawable` ownership redesign, if the drawing classes are ever included.

Net: **more complex**, and the added complexity is concentrated in exactly the teardown path the change is meant to simplify. The line count might come out roughly even; the conceptual load does not.

## Verdict

**Not worth adopting.** Recommend keeping the current model.

### Pros

- Guaranteed destruction at teardown, which V8's weak callbacks genuinely do not provide — the one real, unfixed problem cppgc solves.
- Removes the `--expose-gc` / `kGCCallbackFlagForced` dependency that the current drain quietly rests on.
- Destructor thread affinity becomes structural rather than arranged.
- No V8 rebuild needed; cppgc is already in the shipped monolith.
- Cycles between a JS wrapper and its native object would be collectable, which the current scheme cannot express.

### Cons

- `Drawable`'s `shared_ptr` ownership and game-thread access are fundamentally outside Oilpan's threading model; 5 of ~25 wrapped types cannot move without redesigning the rendering path.
- The leak signal becomes `CHECK`-abort instead of a log line — worse in a game-injected DLL.
- RAII becomes GC-timed for sockets, files and sqlite handles, a user-visible behavioural regression.
- `SQLiteData`/`DBStatementData`'s mutual ownership is the exact UAF pattern the cppgc docs warn about; both need prefinalizers and restructuring.
- On x86 the `CppHeapPointerTag` type check compiles out entirely, so the existing `typeTag_` machinery stays anyway — the wrapper layer gets no simpler.
- Lifetime becomes non-local and harder to debug.
- Added complexity lands in the teardown path, which is where the existing complexity already is.

### What would flip this

1. **Moving to x64.** Pointer compression turns on, `CppHeapPointerTag` range checks become real, and `Wrap`/`Unwrap` could replace the COMDAT-folding tag trick outright. That converts a wash into a genuine simplification of `V8Class.h`.
2. **Teardown leaks becoming measurable.** If dropped second-pass callbacks were observed to retain meaningful memory across many script restarts, the guaranteed-termination argument would carry on its own.
3. **`Drawable` losing its game-thread sharing.** If rendering ever moved behind a snapshot the game thread owns, the one hard blocker disappears and the remaining ~20 types become a mechanical conversion.

Until at least the first of those, the current model — manual `delete` in a weak callback, with the row-recorded instance tracker as the leak check — is the better fit. It is explicit, debuggable, and its known failure mode is a log line rather than an abort.
