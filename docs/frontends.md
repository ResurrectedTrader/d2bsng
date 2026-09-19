# Adding a scripting frontend

What a second frontend costs today, what it should cost, and the shape that
closes the gap.

This is written from measurement, not from taste. A complete SpiderMonkey
frontend was built against this tree - the whole 489-member API surface, the
script engine, events, the game loop, drawables and the dev console - so the
numbers below are what actually happened rather than what seemed likely.

## What a frontend is made of

| layer | lines (V8) | frontend-specific? |
|---|---|---|
| `contract/`, `core/`, `utils/` | - | No. Already engine-free, reused verbatim. |
| `components/` | 14,330 | **Partly** - see below. |
| `api/` | 17,293 | Yes, entirely. |

Of `components/`, **26 of 75 files name V8**. That is the number worth attacking,
and it splits three ways.

### Essential - the engine itself

`components/script/` (9 files) and `components/v8/`. Contexts, compilation,
evaluation, termination, the job queue. This *is* the engine; it cannot be
abstracted away, only implemented again. `components/inspector/` (4 files) is
V8-only outright - SpiderMonkey has no equivalent short of a CDP shim over its
`Debugger` object.

### Incidental - the cheap win

`components/drawing/` (2 files) and two console panels. These name V8 for small,
local reasons:

- `Drawable` holds `onClick` / `onHover` as `v8::Global`s. Nothing else in the
  file is engine-shaped.
- `ScriptPanel` reads `v8::HeapStatistics` and the V8 instance tracker.
- `StacktracesPanel` includes `<v8.h>` **and does not use it**.

The SpiderMonkey port forked all four. Three of those forks were avoidable, and
`StacktracesPanel`'s diff was deleting an unused include.

### Borderline - `components/events/` (6 files)

Events store a JS callback and convert payloads. The stored callback is a GC
reference, and that is the one place where the two engines genuinely disagree on
rules rather than spelling (see *The rooting asymmetry*).

## The evidence for doing this

16 translation units - **5,362 lines** - compiled into both frontends from a
single copy with no abstraction at all, purely because they name no engine type:
the pathfinder, the exit finder, DDE, all of character state, analytics, the
update checker, and eight of the console's twelve files. `Console.cpp` is the
instructive one: every engine-shaped name in it resolves through
`components/script/`, so the same file serves both frontends unmodified.

Sharing is not hard. Leaking is what makes it hard.

## The rooting asymmetry, stated before anything is designed

Any shared type that holds a JS value must obey the **stricter** of the two
engines, which is SpiderMonkey's:

- A root may only be created and destroyed on its owning thread. V8's
  `v8::Global` may be destroyed anywhere.
- A raw value may not be stored anywhere the GC cannot see, and conversions
  therefore write through an out-parameter rather than returning.

So a shared abstraction is **SpiderMonkey-shaped**, and adopting it makes the V8
side slightly more ceremonious for no benefit to V8. That is a real cost and the
reason not to abstract more than necessary. It is worth paying at the boundary -
a handle type and a stats struct - and not worth paying to wrap an engine.

This is also why `Drawable` cannot simply swap `v8::Global` for a portable
"global handle": the game thread holds `shared_ptr<Drawable>` copies across
`DrawAll`, so a root living in the native object would eventually be destroyed
off-thread. The SpiderMonkey port solved it by moving the handlers to the owning
`Script` and leaving two atomic `hasClick` / `hasHover` flags behind, so the
game thread hit-tests without touching JS. That shape is correct for V8 too.

## Proposal: a scripting contract

Mirror what `contract/` already does for the game layer. A small, header-mostly
library that components depend on instead of an engine.

```
src/scripting/            scripting.lib - no engine headers, ever
  Ref.h                   opaque handle to a script value owned by a script
  Invoke.h                call a Ref with arguments, on its owning thread
  Stats.h                 engine numbers, each optional
  Capabilities.h          what this engine can and cannot do
  Frontend.h              the interface a frontend implements
```

### `script::Ref`

An opaque, move-only handle to a JS value, with SpiderMonkey's rules as the
contract:

- created and destroyed on the owning script's thread, and nowhere else;
- copyable only as a reference to the same underlying root;
- carries the owning script's identity so a misuse is detectable.

V8 implements it over `v8::Global`, SpiderMonkey over `PersistentRooted`. The
point is not to hide the engine - it is to give `components/` one thing to hold
that both engines can satisfy, so `Drawable` and the event types stop naming an
engine.

### `script::Stats` - absence is a value

The SpiderMonkey heap panel reports six figures where V8 reports eight, and the
two sets only partly overlap: `external`, `peak malloced`, `physical` and
`global handles` have no `JS_GetGCParameter` equivalent; `unused chunks`,
`nursery` and the GC counters have no V8 equivalent.

So every field is `std::optional`, and the panel renders what is present rather
than a zero that looks like a measurement. A frontend that cannot answer says so.

### `script::Capabilities` - so panels ask instead of assume

```cpp
struct Capabilities {
    bool inspector;         // Chrome DevTools attach
    bool codeCache;         // compiled-code reuse across runs
    bool perBindingProfiling;
    bool heapSnapshots;
};
```

The SpiderMonkey port had to *delete* the settings panel's whole "Debugging (V8
inspector)" section. With a capability it would have greyed out instead, which
is the honest presentation and needs no fork.

### `script::Frontend` - the plug

One interface the glue links against:

```cpp
class Frontend {
public:
    virtual bool Initialize(const config::ScriptPaths&) = 0;
    virtual void Shutdown() = 0;
    virtual Capabilities GetCapabilities() const = 0;
    virtual Stats GetStats() const = 0;
    // ... lifecycle the glue and the game loop need
};
```

`glue/<frontend>-<backend>/` then names one frontend and one backend and does
nothing else. Adding a frontend becomes: implement `Frontend`, provide bindings,
add a glue project.

## On a separate repository

Tempting, and I would not. A separate repo buys isolation and costs version
skew: every contract change becomes a cross-repo bump, and the contract will
move most in exactly the period when a second frontend is being written. The
in-tree `contract/` library already demonstrates the pattern working for the
game layer, with the same benefit and none of the submodule friction.

If the contract is ever wanted by a project outside this tree, extracting a
stable one later is far easier than un-splitting an unstable one.

## On generating the bindings

`api/` is the real duplication: 17,293 lines in the V8 frontend and 20,316 in
the SpiderMonkey one for the same surface. No runtime abstraction touches that -
a binding *is* engine-specific code - so the only way to stop writing it twice is
to stop writing it at all.

The pieces are already in place, which was not true before:

1. The bindings are **already declarative in shape** - `Method(ctx, proto,
   "name", +[]…)` plus structured `///` comments that carry types and
   signatures.
2. `scripts/extract_api.py` already parses that surface with libclang and emits
   it as JSON.
3. `scripts/api_parity.py` can now **prove** that two frontends expose the same
   489 members with the same documented shapes.

That third piece is what makes generation safe to attempt: a generator can be
checked against the hand-written frontend member-for-member, and the check
already gates CI.

The staged version, cheapest first:

- **Now.** Stop the incidental leaks. No new abstraction, no generation.
- **Next.** Introduce `script::Ref` and move the event and drawable callbacks
  onto it, so `components/` names no engine.
- **Then.** Invert the source of truth: today `api.json` is *extracted* from the
  bindings; make it *the input* and generate the bindings from it. Do it one
  class at a time, with the parity gate proving equivalence at each step.
- **Not yet.** A general runtime abstraction over both engines. The measured
  cost is high, it is SpiderMonkey-shaped, and generation makes most of it moot.

## What this is worth

A second frontend today costs roughly 31,000 lines. With the incidental leaks
closed and the events layer on a shared handle, `components/` duplication drops
from about 7,000 lines to the engine itself - call it 3,000. With generated
bindings, `api/`'s 17,000 stops being re-authored at all.

The floor is the engine implementation plus whatever the generator cannot say,
and that is the right floor: it is the part that is genuinely different.
