# Compatibility flags

The scripting engine ships a set of SpiderMonkey/kolbot-era compatibility
behaviors so legacy scripts run unchanged on V8. Each behavior is a named
**compatibility flag**; all flags default to **enabled**. Scripts inspect and
toggle them through the global `Compatibility` object, and a game-version port
can contribute its own flags to the same set.

## The registry

`d2bs::config::CompatibilityFlags` (`src/core/config/CompatibilityFlags.{h,cpp}`)
is the single, process-wide, thread-shared store. It holds an insertion-ordered
list of `{name, defaultEnabled, enabled}` entries behind a mutex (descriptions
are documentation-only - see below).

- `RegisterDefaults()` registers the framework's built-in flags via documented
  `Register("name")` calls (below).
- `Register(name)` / `Register(game::CompatibilityFlag)` add one flag; idempotent by name.
- `IsEnabled(name)` / `Has(name)` / `SetEnabled(name, on)` / `Reset()` / `All()`.

It lives under `core/config/` because it is configuration-shaped and
`config/` is the one component the game implementation (`lod114d`) is allowed to
depend on - so a future port can both contribute flags and query their state
without a new dependency edge.

Registration happens once during framework startup, in `Host::DoInitialize`
right after `game::Bridge::Init()` succeeds and before any script runs: the
framework calls `RegisterDefaults()`, then registers every entry returned by
`game::GetCompatibilityFlags()`.

## Framework flags

| flag | gates |
|------|-------|
| `stringContains` | `String.prototype.contains` / `Array.prototype.contains` aliases (prelude) |
| `errorStackTrace` | the `func@file:line:col` `Error.prepareStackTrace` formatter **and** the raised `Error.stackTraceLimit` (prelude); require.js / LazyLoader parse this format and depend on the depth, so they are one flag |
| `errorSpiderMonkeyProps` | non-standard `Error.prototype` `fileName` / `lineNumber` / `columnNumber` accessors (prelude) |
| `objectToSource` | the `Object.prototype.toSource` shim (prelude) |
| `jsStrictShim` | `js_strict(true);` -> prepend `"use strict";` (CompileSource) |
| `constRunnableRewrite` | `const X = new Runnable` -> `var X = new Runnable` (CompileSource) |
| `profileCallWithoutNew` | calling `Profile(...)` without `new` (JSProfile) |

Each flag is a documented `Register("name")` call in
`CompatibilityFlags::RegisterDefaults()`: the call registers the name (enabled by
default) at runtime, and its `/// @description` comment is the catalog text. The
docs generator (`scripts/extract_api.py`) reads those calls with libclang - the
same machinery as `RegisterConstants` - to emit the `CompatibilityFlag` option
set, rendered as a table in the API docs / `.d.ts`. Descriptions live only in
those comments; they are not stored in the runtime registry.

## Game-version flags

A port exposes version-specific flags by implementing
`game::GetCompatibilityFlags()` (declared in `src/contract/game/Compatibility.h`,
the framework interface). 1.14d returns an empty vector
(`src/backends/lod114d/game/GameHelpers.cpp`). Returned flags are merged into the same
registry, so they are enabled/disabled through the same `Compatibility` object;
the port queries `CompatibilityFlags::Instance().IsEnabled(...)` to act on them.
Game-version flags are registered at runtime; the statically-generated API docs
cover the framework catalog only.

## The `Compatibility` JS object

A non-constructable namespace object (`src/frontends/js/api/classes/scripting/JSCompatibility.h`),
modeled on `TxtTables`. The set of available flag names is documented in the API
docs (the `CompatibilityFlag` option set), so there is no runtime enumeration
method:

```js
Compatibility.enabled()             // string[] of currently-enabled flag names
Compatibility.set(flag, enabled)    // set one (throws TypeError on an unknown flag)
Compatibility.set({ [flag]: bool }) // set many; all-or-nothing (throws before applying on any unknown flag)
Compatibility.reset()               // restore all flags to their defaults (enabled)
```

## When a change takes effect

The flags are read at different points, so a runtime toggle has different reach
per flag:

- **Prelude flags** (`stringContains`, `errorStackTrace`, `errorSpiderMonkeyProps`,
  `objectToSource`) are read in `ApplyCompatibilityPrelude`, once per V8 context
  when a script starts. Toggling affects scripts started afterwards, not a
  context already running.
- **CompileSource flags** (`jsStrictShim`, `constRunnableRewrite`) are read when a
  script's source is compiled. Toggling affects code compiled afterwards.
- **`profileCallWithoutNew`** is checked live in `JSProfile::New`, so it takes
  effect immediately.

## Not flagged

Two behaviors in this area are intentionally **always on**:

- **UTF-8 BOM strip** (`CompileSource`) - source hygiene, never a compatibility
  toggle.
- **The `delay` prelude wrapper** - wraps the native `delay` in a JS function.
  Its stated purpose is to give a tight `while (true) { delay(1000); }` loop a JS
  frame so `stop()` can break out. The actual interrupt path is the cooperative
  `std::stop_token` poll inside `Script::ExecuteEvents` (with `Script::Stop()`
  ordering `request_stop()` before `TerminateExecution()`); a parked native
  `delay` is unreachable by `TerminateExecution()` regardless of any JS wrapper,
  and the enclosing `while` loop is itself the JS frame that services the pending
  termination once `delay` returns. The wrapper is therefore effectively a
  cosmetic no-op, but it is left in place (un-flagged) rather than removed - the
  cost of being wrong is a script that cannot be stopped.
