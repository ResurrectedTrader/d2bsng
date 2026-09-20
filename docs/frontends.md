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

The callback and the call around it are on `script::Ref` and
`script::Invocation`. The payloads are on `script::Value`, a variant of what the
twenty payload builders actually pass - numbers, booleans, strings, raw packet
bytes, and one opaque blob (see *`script::CallArgs`*). So `Events.h` names no
engine: every event *describes* its arguments and one converter per frontend
materialises them.

That now holds for the whole file, the two screen-hook events included - see
*The rooting asymmetry* for the one case that took a different answer.

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

Moving the handlers is only half of it, because the game thread also *dispatches*
them, and pinning a handler at dispatch time copies a root off its owning thread -
legal under V8, forbidden under the stricter rule. So the screen-hook events do
not carry a handler at all. They carry the `shared_ptr<Drawable>` the game thread
already holds plus which of its two handlers they want, and
`Invocation::Run(event, drawable, which)` resolves it out of the owning `Script`'s
map when the event runs - on the script thread, where a value may be made. The
`shared_ptr` is what keeps the map key, which is the drawable's address, from
dangling between dispatch and delivery.

**This changes one observable behaviour.** A handler cleared between dispatch and
delivery - one task-queue hop - now finds nothing to call, where before V8 invoked
the snapshot taken at dispatch. A script that sets `hook.onclick = null` from
another event handler may therefore see a click it previously would have received
go unanswered. This is arguably the more correct reading of "remove the handler",
but it is a change, and it is the only one: the click event's block vote, hover
enter/leave, the leave event `remove()` fires, and teardown ordering are all
unchanged. `Script::RemoveDrawable` keeps its leave event working by dropping the
handler entry *after* dispatching, rather than moving the handler out first.

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
- move-only, because a copy is a second root;
- carries the owning script's identity so a misuse is detectable.

V8 implements it over `v8::Global`, SpiderMonkey over `PersistentRooted`. The
point is not to hide the engine - it is to give `components/` one thing to hold
that both engines can satisfy, so `Drawable` and the event types stop naming an
engine.

It lives at `components/script/ScriptRef.h` today, not in a `src/scripting/`
library, because one consumer does not justify a library. Alongside it is
`Invocation`, which holds the script, the engine and the handlers that script
registered and runs them, so the event types describe *what* to call without
naming *how*. It hands out no isolate and takes no engine handle, so an event
cannot reach the engine through it even by accident.

One call site cannot take the handle, and it is the rule doing its job rather
than a gap: a drawable's click and hover handlers are chosen by the **game**
thread, which may not create a `Ref`. Those two events name the drawable instead
and let `Invocation` resolve the handler on the script thread - see *The rooting
asymmetry*.

### `script::CallArgs` - describe the arguments, don't build them

The third half of making a call, and engine-free outright
(`components/script/CallArgs.h`):

```cpp
using Value = std::variant<bool, int32_t, uint32_t, double, std::string_view, Bytes, Serialized>;
```

Measured against what the events actually pass, that is the whole set. Numbers
keep the width they were written with, because that is what decides whether the
engine makes an integer or a double. `Bytes` is a packet, which becomes a
`Uint8Array`.

`Serialized` looks like the case that breaks it and is not. `scriptBroadcast`
moves a structured-cloned value between scripts **of the same frontend**, so the
blob never crosses engines: it travels opaquely and the frontend that wrote it
reads it back. V8's `ValueSerializer` / `ValueDeserializer` calls live in the
converter, not in the event.

The converter is one function per frontend (`ToEngineValue` in `ScriptRef.cpp`).
An event that needs an engine *operation* rather than an argument cannot go
through it - the console REPL's `EvaluateEvent` compiles and runs a snippet - so
`Invocation` grew an `Evaluate` for it, which is the same answer as `Run`: name
the operation, let the frontend perform it.

### `script::Stats` - absence is a value

The SpiderMonkey heap panel reports six figures where V8 reports eight, and the
two sets only partly overlap: `external`, `peak malloced`, `physical` and
`global handles` have no `JS_GetGCParameter` equivalent; `unused chunks`,
`nursery` and the GC counters have no V8 equivalent.

So every field is `std::optional`, and the panel renders what is present rather
than a zero that looks like a measurement. A frontend that cannot answer says so.

### `script::EngineInfo` - so panels ask instead of assume

The SpiderMonkey port had to *delete* the settings panel's whole "Debugging (V8
inspector)" section, and could not have kept the rest of the panel anyway: it
called `v8::V8::GetVersion()` directly. Both are the same mistake - the panel
knowing which engine it is drawing - and the same fix.

The engine reports itself, from `components/script/ScriptTypes.h` next to
`HeapStats` (the engine-free types header the panels already reach through, in a
directory a second frontend reimplements). `ScriptEngine::GetEngineInfo()` is the
answer:

```cpp
struct EngineInfo {
    std::string_view name;  // as a user should see it, e.g. "V8"
    std::string version;
    bool inspector;         // attach a remote debugger (Chrome DevTools)
};
```

Identity and capability travel together because the panel needs both in the same
breath: the inspector section is headed with the engine's name and gated on
whether it has one. With the name and version reported, `components/console/`
contains no V8 API call at all - the settings rows read "Engine flags", "Engine
threads" and "Engine", and the last shows "V8 15.6.8" because the engine said so.

One capability, because one is all that differs. The inspector is a Chrome
DevTools endpoint and SpiderMonkey has none short of a CDP shim over its
`Debugger` object. Everything else the console offers, both engines do: the
SpiderMonkey port has the binding trampolines (`DefineFunctionWithReserved`) that
feed the Profiling panel's per-binding attribution and the stack capture
(`JS::CaptureCurrentStack`) behind the Stacktraces panel, so a flag for either
would have no `false` case and gate a branch nobody takes. The earlier sketch also
proposed a code-cache flag and a heap-snapshot flag: the cache is transparent and
nothing in the UI reads it, and heap figures are already answered at a finer grain
- every `HeapStats` field is `std::optional`, so an engine says per figure what it
cannot report and the panel renders the absence.

Absent, not disabled. A control can be greyed out when it is *temporarily*
unavailable - something the user could switch on - but an engine without a
debugger will never have one, and a permanently dead section with an explanatory
tooltip is clutter that never becomes useful. So the section is drawn only where
the capability is reported, which is the presentation the SpiderMonkey port
arrived at by hand; the difference is that it costs a condition rather than a
fork.

The engine's tuning settings are neutral for the same reason: `AppConfig` carries
`engineFlags` / `engineThreadPoolSize` / `engineSingleThreaded`, read from
`[settings]/EngineFlags`, `/EngineThreadPoolSize` and `/EngineSingleThreaded`.
The `V8*` keys those replace are not read at all - a clean break, so an INI
carrying them falls back to the defaults.

The thread settings are portable in substance, not just in name: "how many worker
threads may the engine use" is a question both engines answer, even though V8
sizes the default platform it creates and SpiderMonkey is told a count for the
pool its embedder supplies (`js::SetHelperThreadTaskCallback`, `JSGC_MAX_HELPER_THREADS`).
The flags string is not: V8 parses it with `SetFlagsFromString`, SpiderMonkey has
no string parser at all (typed prefs in `js/Prefs.h`). So the value is opaque and
belongs to whichever engine is built - the neutral *name* says which setting it
is, not that its contents travel.

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
- **Next.** Introduce `script::Ref` and `script::Value` and move the event
  callbacks and payloads onto them, so `components/events/` names no engine.
  Done - `components/events/` no longer names V8 anywhere.
- **Then.** Invert the source of truth: today `api.json` is *extracted* from the
  bindings; make it *the input* and generate the bindings from it. Do it one
  class at a time, with the parity gate proving equivalence at each step.
  `script::Value` is a first read on how far a described argument gets: it
  covered every event payload, but events only ever *pass* values, and a binding
  also reads arguments, throws, and hands back wrapped native objects.
- **Not yet.** A general runtime abstraction over both engines. The measured
  cost is high, it is SpiderMonkey-shaped, and generation makes most of it moot.

## What this is worth

A second frontend today costs roughly 31,000 lines. With the incidental leaks
closed and the events layer on a shared handle, `components/` duplication drops
from about 7,000 lines to the engine itself - call it 3,000. With generated
bindings, `api/`'s 17,000 stops being re-authored at all.

The floor is the engine implementation plus whatever the generator cannot say,
and that is the right floor: it is the part that is genuinely different.
