# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

d2bsng (D2 Botting System Next Generation) is a Windows DLL that exposes JavaScript scripting capabilities via Google's V8 engine for Diablo II automation. The JS API is implemented against Diablo II 1.14d (LoD): the game layer reads live game state through a typed imports layer built on D2MOO. Versions start at 2.x (legacy d2bs topped out at 1.6.x) so scripts can tell the two apart.

## Documentation

Design docs live in `docs/`. Read the one(s) covering whatever you are about to touch before changing it, and update them in the same change rather than duplicating their content here:

- `docs/coords.md` - the two D2 coordinate spaces (subtiles vs game coords) and where the subtile -> game-coord conversion lives (game-layer stubs only).
- `docs/game_thread_safety.md` - identity-based handles, per-frame pointer caching (`HandleCache`), and the game read/write lock model.
- `docs/window_message_handling.md` - the `WH_GETMESSAGE` input hook (block / dispatch / injected-input tagging), the game-window WndProc subclasses, and the console raw-input summon.
- `docs/inspector.md` - the V8 inspector (Chrome DevTools) attachment: the ixwebsocket transport, the InspectorServer / InspectorTarget / ScriptInspector split, the inbound-queue threading model, and releasing game locks during a breakpoint pause.
- `docs/compatibility.md` - the scripting compatibility-flag system: the `CompatibilityFlags` registry, the framework flag catalog, the `game::GetCompatibilityFlags()` extension point, the `Compatibility` JS object, the per-flag gating sites, and why the BOM strip and `delay` wrapper stay un-flagged.

## Build Commands

`build.ps1` (PowerShell) is the build entry point. The actual build is MSBuild over `d2bsng.slnx`; the script just locates the toolchain and dispatches.

```bash
# From Cygwin/bash, invoke via PowerShell:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build.ps1 Release

# Available targets:
#   Release        - Release build (default, recommended)
#   Debug          - Debug build
#   format         - Format source files with clang-format
#   check-format   - Check formatting (fails if files need formatting)
#   lint           - clang-tidy analysis (delegates to scripts/lint.ps1)
#   fix            - Auto-fix clang-tidy violations
#   test           - Build and run the test suite (js_tests.exe)
```

The build script auto-detects the Visual Studio installation via vswhere and works from any directory. You can also build directly with MSBuild or in Visual Studio.

## Tests

The test project (`tests/frontends/js/js_tests.vcxproj`) is a standalone console exe using [doctest](https://github.com/doctest/doctest). It compiles the real `Pathfinder.cpp` with fake game layer implementations (no DLL, no V8, no game). Tests do not link any static libs - they compile sources directly alongside test fakes.

```bash
# Build and run all tests:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build.ps1 test

# Run specific tests by name filter:
Release/js_tests.exe -tc="Walk A*"

# List all test cases:
Release/js_tests.exe -ltc

# Verbose output (show all assertions + benchmark messages):
Release/js_tests.exe -s

# Run only benchmarks:
Release/js_tests.exe -tc="Benchmark*,Kurast*,Real*" -s
```

### What the tests cover

Currently tests only cover the **pathfinding engine** (`src/frontends/js/components/pathfinding/`):

- **Unit tests** (`tests/frontends/js/pathfinding/tests/`): CollisionLookup queries, walk A*, teleport A*, walk reduction, point mutation, edge cases, penalty avoidance
- **Reference comparison** (`tests/frontends/js/pathfinding/tests/comparisons/`): exact-match and cost-equivalence tests comparing our A* output against an adapted reference A* implementation on identical collision grids
- **Benchmarks**: synthetic 2000x2000 grids and real game collision data (`.d2col` fixtures in `tests/frontends/js/fixtures/maps/`) comparing walk and teleport modes against the reference
- **Fixture system** (`tests/frontends/js/fixtures/`): binary `.d2col` loader for collision grids dumped from the game via `tools/dump_collision.js`

### Adding real game data for benchmarks

1. Load `tools/dump_collision.js` in d2bs while in-game - dumps `.d2col` files per level
2. Copy `.d2col` files to `tests/frontends/js/fixtures/maps/`
3. Run `.\build.ps1 test` - real-world benchmarks auto-discover and use them

**Requirements**: Visual Studio 2022 with ClangCL toolchain, Windows SDK 10.0.26100.0

## API Documentation

The script-visible JS API is documented by a generator pipeline under `scripts/`. The bindings in `src/frontends/js/api/` carry structured `///` doc comments (`@description`, `@signature`, `@param`, `@returns`, `@throws`, `@callback`, `@event`, `@type`, `@mode`); their exact vocabulary lives at the top of `extract_api.py`. The `{type}` fields may be object literals / unions / generics (`{x:number,y:number}`, `Unit|null`, `Array<{x:number}>`) - the parser is brace-balanced.

**Enumerations as option sets**: a `{type}` that names a C++ `enum class` (libclang-parsed from `game/Types.h` / `game/Constants.h`) or a constant namespace (e.g. `ProfileType`) is rendered as a value table, not prose. `extract_api.py` reads the enumerators with libclang - plus the compatibility flags from `CompatibilityFlags::RegisterDefaults()`'s documented `Register("name")` calls - into an `enums` map (only sets referenced by some `{type}` are emitted); `gen_api_docs.py` renders an Enums section and auto-links the type; `gen_dts.py` emits a literal-union alias (`type Difficulty = 0 | 1 | 2 | 3`; bitfields and constant namespaces map to `number`). A bitfield enum is typed `number` (a value is an OR-combination) by marking it `/// @flags`. So document an enum-valued member as `@type {Difficulty}` and keep the values in the C++ enum, not in the description.

| Script | Flow | Notes |
|--------|------|-------|
| `extract_api.py` | `src/frontends/js/api/**` -> `api.json` | Walks the V8 bindings with **libclang**, emitting the full surface (classes, globals, constants, the `me` object, events, enums). Needs the same vcpkg + V8 + MSVC/SDK include set the build uses. The only script that needs libclang. |
| `gen_dts.py` | `api.json` -> `d2bsng.d.ts` | Ambient TypeScript declarations (JSDoc on every member, typed `addEventListener` overloads) for editor completion. Pure-stdlib. |
| `gen_api_docs.py` | (`api.json`) -> `index.html` | Emits the static docs **shell**: an embedded JS renderer + CSS, no CDN. An optional baked-in `api.json` is the offline / instant-paint dataset. Pure-stdlib. |
| `build_docs_site.py` | releases -> `site/` | Deploy-time assembler: pulls every release's `api.json` server-side (via `gh`) into `data/<tag>.json` + a `versions.json` manifest, then wraps the shell. Pure-stdlib + `gh`. |

```bash
python scripts/extract_api.py -o api.json
python scripts/gen_dts.py api.json -o d2bsng.d.ts
python scripts/gen_api_docs.py api.json -o api.html   # offline single-version page
```

### Versioned docs site (GitHub Pages)

`release.yml` ships `api.json` + `d2bsng.d.ts` as release assets, then a `deploy-docs` job (after the release is published, so the new asset exists) runs `build_docs_site.py` to gather **every** release's `api.json` into a same-origin bundle and publish it to Pages. The page is static and makes **no runtime GitHub API calls** (no rate limits / CORS / visitor auth): it reads `versions.json` + `data/<tag>.json`, offers a version selector, and a Changelog tab showing the release notes plus an auto API-surface diff (added / removed / changed symbols) against the previous version.

**One-time setup**: enable Pages with **Settings -> Pages -> Source = "GitHub Actions"**, or `deploy-docs` fails (it does not block the binary release).

## Development Setup

```cmd
# Setup git hooks (format checking on commit)
git config core.hooksPath .githooks
```

## Worktrees

Creating a git worktree needs two extra steps because of the heavy,
partly-out-of-tree dependencies:

- `dependencies/v8/include/**` is tracked but deeply nested. Under the long
  `.claude/worktrees/<name>/...` prefix those paths overflow MAX_PATH and git
  silently aborts the checkout (leaving a half-populated worktree), so
  `core.longpaths` must be enabled first.
- `dependencies/v8/libs` (the V8 monolith `.lib`s) is gitignored and
  `dependencies/D2MOO` is a submodule - neither is populated by `git worktree
  add`. Point both at the main checkout with a directory junction (not a copy or
  re-fetch).

Recipe (PowerShell, from the main checkout root):

```powershell
$main = (Get-Location).Path
$wt   = "$main\.claude\worktrees\<name>"
git config core.longpaths true                      # else deep v8 headers fail to check out
git worktree add -b <branch> $wt <base-ref>
# V8 monolith libs (gitignored) - junction from main
New-Item -ItemType Junction -Path "$wt\dependencies\v8\libs" -Target "$main\dependencies\v8\libs"
# D2MOO submodule (empty placeholder) - remove, then junction from main
# (alternatively: git -C $wt submodule update --init dependencies/D2MOO)
[System.IO.Directory]::Delete("$wt\dependencies\D2MOO", $false)
New-Item -ItemType Junction -Path "$wt\dependencies\D2MOO" -Target "$main\dependencies\D2MOO"
```

`build.ps1` does `Set-Location $PSScriptRoot`, so run the *worktree's* `build.ps1`
to build the worktree. Its output stays under the worktree's `Release\`, so it
won't collide with a running game that loaded the main checkout's `d2bs.dll`.

Removing a worktree - **detach the junctions first**. Any recursive delete that
follows them (`git worktree remove`, `rm -rf`, `Remove-Item -Recurse`) walks into
the junction targets and deletes the *main* checkout's V8 libs (gitignored, slow
to rebuild) and D2MOO submodule contents. A non-recursive delete removes the
junction itself and leaves the target intact:

```powershell
$main = (Get-Location).Path
$wt   = "$main\.claude\worktrees\<name>"
# Detach the junctions BEFORE deleting anything - the target stays intact.
[System.IO.Directory]::Delete("$wt\dependencies\v8\libs", $false)
[System.IO.Directory]::Delete("$wt\dependencies\D2MOO", $false)
git worktree remove $wt        # --force if the tree has uncommitted changes
# git branch -D <branch>       # optional: also drop the worktree's branch
```

If `git worktree remove` refuses with "'.git' is not a .git file" - a worktree
whose internal links were written by Cygwin git (`/cygdrive/...` paths that
Git-for-Windows can't parse) - detach the junctions as above, then remove the
tree and its admin entry by hand and prune. Delete from a Cygwin / Git-Bash
shell (`rm -rf "$wt"`) if the deep `v8/include` paths overflow MAX_PATH for
`Remove-Item`:

```powershell
Remove-Item -Recurse -Force $wt
Remove-Item -Recurse -Force "$main\.git\worktrees\<name>"
git worktree prune
```

Either way, confirm `dependencies\v8\libs` and `dependencies\D2MOO` in the main
checkout still hold their files afterward.

## Git

Commits should not be GPG signed:
```bash
git commit --no-gpg-sign -m "message"
```

## Architecture

### Project Structure

The codebase is split into six build targets (five static libs + one DLL) under `src/`, plus a test project. A frontend (JS) and a backend (1.14d) both compile against a shared `contract`, and a thin glue project links one of each into the final DLL:

```
d2bsng/
├── build.ps1               Build entry: build / Debug / format / check-format / lint / fix / test
├── vcpkg.json              Single shared vcpkg manifest (all deps)
├── dependencies/
│   ├── v8/                 V8 engine (vendored headers; monolith lib obtained separately)
│   └── D2MOO/              D2MOO submodule - game struct/function reference
├── docs/                   Design docs (coords, thread-safety, window messages, inspector)
├── scripts/                Maintainer tooling (lint.ps1, API extraction + docs/d.ts/site generators, table generators)
├── src/
│   ├── utils/              utils.lib - standalone utilities (crypto, threading, stackwalker)
│   ├── contract/           contract.lib - the boundary both frontends and backends compile against
│   │   ├── game/               Game interface headers (NO .cpp) + framework-owned utilities
│   │   └── config/             Shared DTOs: ProfileData, ScriptPaths
│   ├── core/               core.lib - shared infrastructure (depends on contract)
│   │   ├── config/             AppConfig, IniConfigStore, CompatibilityFlags, Version
│   │   ├── speedhack/          Global game-time scaling
│   │   └── proxy/              SOCKS5 bypass scope for script sockets
│   ├── frontends/          One directory per scripting frontend
│   │   └── js/             js.lib - JavaScript scripting frontend (V8); depends on contract + core
│   │       ├── api/            V8 bindings: classes/ (game, io, scripting, drawing), globals/, core/
│   │       ├── components/     Frontend internals:
│   │       │   ├── script/         Script engine (V8 isolate mgmt) + compat shims
│   │       │   ├── v8/             V8 host initialization
│   │       │   ├── events/         Event system
│   │       │   ├── gameloop/       Per-frame game-thread loop + lock release
│   │       │   ├── pathfinding/    A* pathfinder
│   │       │   ├── console/        ImGui dev console (log/REPL/scripts/stacktraces/threads/settings)
│   │       │   ├── inspector/      V8 inspector (Chrome DevTools) debug server
│   │       │   ├── drawing/        Screen-hook drawables (Box/Frame/Line/Text/Image)
│   │       │   ├── characterstate/ Character-state snapshot -> D2BotNG manager (WM_COPYDATA)
│   │       │   ├── dde/            DDE service
│   │       │   ├── exits/          Level-exit finder
│   │       │   ├── profile/        ProfileService (profile lookup/switch logic)
│   │       │   ├── update/         GitHub-release update checker (6h poll -> version-banner marker)
│   │       │   └── Host.h/.cpp     Frontend lifecycle (d2bs::js::Host) + GameCallbacks wiring
│   ├── backends/           One directory per game-version backend
│   │   └── lod114d/        lod114d.lib - 1.14d game backend (implements contract); depends on contract + core
│   │       ├── game/           1.14d implementation (.cpp + internal .h)
│   │       ├── imports/        Typed game func/var registry + 1.14d offsets + extras/ structs
│   │       ├── hooks/          Inline / IAT hooks (incl. WS2_32 connect SOCKS5 hook)
│   │       ├── asm_thunks/     Hand-written ABI thunks
│   │       └── console/        Port console host (window + GL + ImGui glue)
│   └── glue/               One directory per frontend+backend combo (the shippable target)
│       └── js-lod114d/     d2bs.dll - glue: DllMain + wiring; links js + lod114d + contract + core + utils
│           ├── dllmain.cpp     DLL entry point (Bridge::Init -> InstallAll -> js::Host::Initialize)
│           └── version.rc      DLL version resource
└── tests/
    └── frontends/
        └── js/             js_tests.exe - doctest suite (pathfinding) + fakes
```

### Build Targets

| Target | Type | Output | Contents |
|--------|------|--------|----------|
| **utils** | Static lib | `Release/utils.lib` | `src/utils/` - crypto, threading, stackwalker |
| **contract** | Static lib | `Release/contract.lib` | `src/contract/` - game interface headers (`game/*.h`) + framework-owned utilities + shared DTOs (`config/ProfileData.h`, `config/ScriptPaths.h`). The boundary both frontends and backends compile against. Depends on utils. |
| **core** | Static lib | `Release/core.lib` | `src/core/` - shared infra: config (AppConfig/Ini/CompatibilityFlags/Version), speedhack, proxy. Depends on contract + utils. |
| **js** | Static lib | `Release/js.lib` | `src/frontends/js/` - JavaScript scripting frontend (api/, components/). Depends on contract + core + utils + V8. Has unresolved game:: symbols. |
| **lod114d** | Static lib | `Release/lod114d.lib` | `src/backends/lod114d/` - 1.14d game backend implementing the contract. Depends on contract + core + utils. No frontend dependency. |
| **d2bs** | DLL | `Release/d2bs.dll` | `src/glue/js-lod114d/` - glue: DllMain + version.rc. Links js + lod114d + contract + core + utils, resolves all symbols. |
| **js_tests** | Console EXE | `Release/js_tests.exe` | `tests/frontends/js/` - doctest tests with fake game layer |

### How Linking Works

```
utils.lib       <- fully resolved, standalone
     v
contract.lib    <- game interface headers (game/*.h) + shared DTOs. Depends on utils.
     v              (declares game::Unit::Pos() etc.; impl lives in a backend)
core.lib        <- config / speedhack / proxy. Depends on contract + utils.
     v
     +----------------------------+----------------------------+
     v                            v
js.lib                        lod114d.lib    <- frontend and backend are mutually blind:
(JS/V8 frontend)              (1.14d backend)   js has UNRESOLVED game:: symbols;
has UNRESOLVED game::         implements them    lod114d implements them and calls UP only
symbols; depends on          ; depends on        through the GameCallbacks function table
contract + core              contract + core
     v                            v
     +-------------+--------------+
                   v
               d2bs.dll   <- glue: DllMain + wiring. Links js + lod114d +
                             contract + core + utils + v8_monolith.lib.
                             LTO inlines the thin game:: wrappers across all libs.
```

### Game Interface vs Implementation

The game abstraction is split across two directories:

- **`src/contract/game/`** - Interface headers only (18 `.h` files). Defines the wrapper classes (`Unit`, `Room`, `Level`, etc.) with method declarations using opaque `void*` pointers. Part of `contract.lib`. No game-version-specific code. Both the frontend and the backend compile against it.

- **`src/backends/lod114d/game/`** - 1.14d implementation (12 `.cpp` files + internal headers like `RoomData.h` / `DrlgHelpers.h`). Part of `lod114d.lib`. The version-specific game-function/variable bindings, structs, and hooks live alongside it under `src/backends/lod114d/imports/` (typed import registry + per-DLL declarations), `src/backends/lod114d/imports/extras/` (structs not in D2MOO), `src/backends/lod114d/asm_thunks/`, and `src/backends/lod114d/hooks/`.

To add support for a different game version: create a new backend lib (a sibling of `backends/lod114d/`) with its own `game/` implementation and vcxproj, depending on `contract` + `core` + `utils`, then a glue project under `glue/` that links it with a chosen frontend. To add a different frontend (a non-JS scripting host, or a C-ABI bridge that re-exports the contract for other languages): create a sibling of `frontends/js/` over the same `contract` + `core`. Frontend and backend never reference each other - only the glue does.

### Include Path Strategy

Each project has specific include directories that make cross-project includes work:

| Project | Include Directories |
|---------|-------------------|
| **utils** | `$(ProjectDir)` |
| **contract** | `$(ProjectDir)` ; `$(SolutionDir)src` |
| **core** | `$(ProjectDir)` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src` |
| **js** | `$(ProjectDir)` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src` ; V8 include |
| **lod114d** (backend) | `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src` ; `$(ProjectDir)` ; `$(ProjectDir)game` ; D2MOO include roots ; V8 include |
| **d2bs** (glue DLL) | `$(SolutionDir)src\frontends\js` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src\backends\lod114d` ; `$(SolutionDir)src` |
| **js_tests** | `$(ProjectDir)fakes\shims` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src\frontends\js` ; `$(SolutionDir)src` ; `$(ProjectDir)` |

All includes use project-root-relative paths - never use relative paths like `../`:
```cpp
#include "game/Unit.h"                    // Interface header (from contract)
#include "api/core/V8Class.h"             // Frontend internal (js)
#include "config/AppConfig.h"             // Config (from core)
#include "config/ProfileData.h"           // Shared DTO (from contract)
#include "utils/utils.h"                  // Cross-project utils include
#include "RoomData.h"                     // Same-dir include (in backends/lod114d/game/)
```

Same-directory includes use quotes without path: `#include "ClassRegistry.h"`

### Load-bearing includes (do not strip in "unused include" cleanups)

Some `#include`s name no symbol directly in the including file yet are required
to compile or link it. A "remove unused includes" pass (ReSharper's unused-include
cleanup, clang-tidy IWYU, etc.) cannot see why they are needed and will strip
them, breaking the build in non-obvious ways. When an include is genuinely
load-bearing but looks unused, guard it with a suppression comment on the line
directly above it:

```cpp
// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
```

The recurring categories in this codebase:

- **Macro-configuration headers.** `imports/D2MOOConfig.h` defines the
  `*_DLL_DECL` macros (`STORM_DLL_DECL`, `D2COMMON_DLL_DECL`, ...) and the D2
  version-selection macros that the vendored D2MOO headers expand. Every
  `imports/*.h` that transitively pulls in a D2MOO header includes it first
  (above `ImportTypes.h`); without it you get `unknown type name 'STORM_DLL_DECL'`.
  It names no symbol used by the including header, so it always looks unused.
- **Macro-driven implementation.** `tests/frontends/js/test_main.cpp` is the one TU
  that defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` and then includes
  `<doctest/doctest.h>`; that include is what emits doctest's runner, registry,
  and `main()`. Drop it and every test file's static registrar fails to link
  (`undefined symbol: doctest::detail::TestCase::operator*`).
- **Complete type for an out-of-line defaulted dtor/ctor.** A `.cpp` that defines
  `= default` ctor/dtor for a class holding `std::unique_ptr<T>` of a
  forward-declared `T` must include `T`'s full definition (e.g. `AppConfig.cpp`
  includes `ConfigStore.h`) - the implicit `~unique_ptr` instantiation needs the
  complete type. The include looks unused because no member of `T` is named in
  the `.cpp`. (See also the unique_ptr entry under "Common clang-tidy Pitfalls".)
- **Transitive type providers (fix by IWYU, not by re-adding to the intermediary).**
  When a header stops handing a type to a transitive consumer, add the include to
  the file that actually uses the type rather than restoring it in the
  intermediary. Example: `GameHelpers.cpp` dereferences `D2InventoryGridInfoStrc`
  (`entry->layout->...`), whose full definition lives in `DataTbls/InvTbls.h`;
  `D2Common.h` only forwards `D2Inventory.h` (a forward declaration of that type),
  so the consumer includes `InvTbls.h` directly. Do not re-add such an include to
  a header that does not use the type - that re-introduces the redundancy the
  cleanup removed. (Conversely, `D2Constants.h` / `LevelsTbls.h` / `ObjectsTbls.h`
  / `D2BitManip.h` are correctly reached through `D2Common.h`, which includes and
  uses them, so their removal from other files is fine.)
- **Inline definitions split from their declaration.** `game/Finders.h` holds the
  inline bodies of the handle classes' `Find*` / `Get*` methods, while the
  declarations live in `Unit.h` / `Control.h` / `Level.h` / `Party.h`. A `.cpp`
  that calls one of these compiles against the declaration alone, so the call site
  looks like it needs only the handle header - but without `Finders.h` the inline
  body is emitted in no TU and you get a link-time `undefined symbol` (e.g.
  `Control::Find`, `Unit::FindFirst`, `Level::GetPresetUnits`). Any TU that calls
  a `Finders.h` method must include it.

After any unused-include cleanup, build both the DLL (`build.ps1 Release`) and the
tests (`build.ps1 test`) - the compiler and linker are the only reliable check
that a stripped include was actually unused.

### Dependency Rules

These are the intended dependencies. A few deliberate exceptions are noted inline - each one lets a layer reuse an existing type or helper instead of duplicating it, which is the cheaper trade-off than the indirection that removing the edge would require.

- **utils/** depends on: standard library, Windows headers, third-party libs (spdlog, stackwalker).
- **contract/** (the boundary) depends on: utils + standard library only. NEVER on core, the frontend, V8, or any backend. Holds the game interface (`game/*.h`), the framework-owned utilities (Finders/GameLock/GameThread/HandleCache/Types), and the shared DTOs (`config/ProfileData.h`, `config/ScriptPaths.h`). `game/Menu.h` includes `config/ProfileData.h` (same project) so `Login()` takes the profile struct by const-ref.
- **core/** (shared infra) depends on: contract + utils. Holds config (AppConfig/Ini/CompatibilityFlags/Version), speedhack, proxy. NEVER on the frontend or a backend.
- **frontends/js/** (JavaScript frontend) depends on: contract + core + utils + V8. Reaches the game only through `game::` contract symbols (resolved at the glue link) and pushes its hooks down through the `GameCallbacks` table; it NEVER references a concrete backend.
  - **frontends/js/api/** depends on: contract (game/ interface + DTOs), core, components/, utils/, V8.
  - **frontends/js/components/** depends on: contract, core, utils/, V8. Exception: `components/script/` includes `api/` - the script engine is the JS-API composition root (it owns V8 isolate setup and registers the `api/` ClassRegistry + globals), and a few components reuse `api::v8_convert`. `components/update/` reuses the V8-free `api::classes::PerformHttpRequest` (`api/classes/io/HttpEngine.h`).
- **backends/lod114d/** (1.14d backend) depends on: contract + core + utils, plus sibling port headers (imports/, hooks/, asm_thunks/). Implements the `game::` contract symbols and calls UP into the frontend ONLY through the `GameCallbacks` pointers it is handed at init (`hooks::GetActiveCallbacks()`). NEVER on the frontend, api/, or V8. Config reads go through `core`; console output and rendering go through the `onConsoleMessage` / `onConsoleDrawFrame` callbacks.
- **glue/js-lod114d/** (glue) depends on: js + lod114d + contract + core + utils. The only project that sees both a frontend and a backend; owns `DllMain` and the bring-up wiring.

### Key Design Decisions

- **32-bit only**: All projects currently target Win32 (1.14d game compatibility).
- **ClangCL compiler**: Uses LLVM/Clang with MSVC compatibility
- **Static linking**: VCPKG dependencies and CRT statically linked (MT/MTd runtime)
- **C++23**: Uses latest C++ standard
- **LTO enabled**: `WholeProgramOptimization=true` in Release - wrapper methods inline across TUs and static libs

### Dependencies (via VCPKG)

Single `vcpkg.json` at solution root, shared by all main projects:
- spdlog + fmt: Logging
- stackwalker: Stack traces (DbgHelp-backed; reads PDB symbols)
- sqlite3: Database support
- detours: Microsoft Detours - function hooking
- imgui (opengl2 + win32 bindings): dev console UI

V8 (JavaScript engine) is not a vcpkg dependency - it is a custom monolithic build in `dependencies/v8/`.

The test project (`tests/frontends/js/`) has its own `vcpkg.json` with just `doctest` (header-only test framework).

## Game Abstraction Layer

The game layer decouples the JS API from direct game memory access, enabling multi-version support.

### Design Principles

1. **No V8 dependency**: The game interface uses only standard C++ types. No V8 headers, no API layer headers.
2. **Thin wrappers**: Each wrapper class is `sizeof(void*)` - one pointer, no vtable. Zero overhead with LTO.
3. **Gaps marked with TODO**: Methods read live game state; the few unimplemented spots are marked `TODO(implement)` with comments describing what is needed.
4. **Opaque pointers**: Wrappers hold a single `void*`. The framework interface stays game-version-agnostic; typed D2MOO structs are used only inside the implementation layer (`src/backends/lod114d/`), never at the framework boundary.

### File Organization

`src/contract/game/` hosts **two kinds** of headers (plus the shared DTOs in `src/contract/config/` - `ProfileData.h`, `ScriptPaths.h`):

1. **Abstraction interfaces** - declarations implemented per-game under `src/backends/lod114d/game/` (or future ports like 1.13c, D2R). Port authors must provide `.cpp` files for these: `Unit.h`, `Room.h`, `Level.h`, `Party.h`, `Control.h`, `Sprite.h`, `Menu.h`, `Console.h`, `Bridge.h`, `GameCallbacks.h`, and the game-specific declarations in `GameHelpers.h`.

2. **Shared utilities** - fully implemented in `contract` (header-only / inline), operate on the interfaces above, have no game-specific counterpart. Port authors implement nothing here; they get these for free: `Finders.h`, `Types.h`, `HandleCache.h`, `GameLock.h`, `GameThread.h`, `Constants.h`.

Within each handle header (`Unit.h`, `Room.h`, etc.), a comment separator marks the bucket-1 (game-impl required) and bucket-2 (framework-impl, inline in `Finders.h`) sections of the class.

**Interface** (`src/contract/game/`):

| File | Purpose |
|------|---------|
| `Bridge.h` | Static `Init()`/`Shutdown()` interface |
| `Unit.h` | `game::Unit` wrapper - 80+ methods covering all unit operations |
| `Room.h` | `game::Room` wrapper (Room2/D2DrlgRoomStrc) |
| `Level.h` | `game::Level` wrapper (D2DrlgLevelStrc) |
| `Control.h` | `game::Control` wrapper (D2WinControlStrc) |
| `Party.h` | `game::Party` wrapper (D2RosterUnitStrc) |
| `Sprite.h` | `game::Sprite` wrapper |
| `Menu.h` | Out-of-game menu state classification + OOG actions (Login, CreateGame, ...) |
| `Console.h` | Port-chosen `console::OnMessage` sink + color-code split helpers |
| `GameHelpers.h` | Free functions: game state queries, drawing, network, trade, OOG actions |
| `GameCallbacks.h` | Function pointer struct for game -> framework event callbacks |
| `Finders.h` | Framework-owned filtered searches / composed walks (inline method defs on handle classes) |
| `Types.h` | Shared 2D geometric primitives (`Point`, `Position`, `Size`) |
| `HandleCache.h` | Per-frame pointer cache for identity-based handles |
| `GameLock.h` | `GameReadLock` / `GameWriteLock` primitives |
| `GameThread.h` | `GameThread::Execute()` - post work to game thread |
| `Constants.h` | Game-layer shared constants / enums |

**Implementation** (`src/backends/lod114d/game/` - 1.14d specific):

| File | Purpose |
|------|---------|
| `game/*.cpp` | Implementations of all interface methods |
| `imports/ImportTypes.h` | Typed import registry: `GameFunc<Cc,Sig>` / `GameVar<T>`, resolved against the module base in `Bridge::Init()` |
| `imports/*.h` (D2Client, D2Common, D2Game, D2Win, ...) | Per-DLL game function / variable declarations with their 1.14d offsets |
| `imports/extras/*.h` | Game structs NOT in D2MOO, with `static_assert` size checks |
| `imports/D2MOOConfig.h` | Selects the 1.14d D2MOO build |
| `asm_thunks/*`, `hooks/*` | Hand-written ABI thunks and inline / IAT hooks |

### Framework vs Game Separation

| Belongs in backends/lod114d/game/ (impl) | Belongs in core/config/ | Belongs in js frontend |
|---|---|---|
| Game memory reads (ping, fps, unit stats) | Bot settings (chickenHp, blockKeys) | Console UI (ShowConsole, HideConsole) |
| Game function calls (draw, click, trade) | Profile name | Skill name tables (Game_Skills[]) |
| Game UI controls (findControl, clickControl) | Script base path | IPC (SendCopyData) |
| OOG actions (login, createGame via controls) | | Timers/events (setTimeout, addEventListener) |
| Coordinate transforms (ScreenToAutomap) | | Profile management (addProfile, d2bs.ini) |

### TODO Tag Convention

| Tag | Location | Meaning |
|-----|----------|---------|
| `TODO(implement)` | (any) | Functionality not yet implemented (currently the character-create name entry in Menu.cpp) |

## Reference Implementation

The `reference/d2bs/` directory contains the original d2bs implementation (SpiderMonkey-based) used for cross-referencing when implementing the V8 API. Key files:

- `JS*.cpp` / `JS*.h` - JavaScript API implementations (JSUnit, JSControl, JSFile, etc.)
- `D2Structs.h` - Game structure definitions
- `D2Ptrs.h` - Game function pointers and offsets (source of truth for `Offsets.h`)
- `D2Helpers.cpp` - Helper functions (GameReady, SetSkill, GetSkill, etc.)
- `Constants.h` - Game constants and enums
- `JSGlobalFuncs.h` - Master function registration table

When implementing functionality marked with `TODO(implement)`, reference the corresponding file for the original implementation logic.

**Don't silently "fix" reference quirks scripts work around.** Some reference behaviors are genuinely buggy, but existing JS scripts can compensate for them, so quietly changing one can break the bots. Default to preserving the reference behavior with a comment explaining the quirk, and treat any deliberate fix as a JS API change requiring explicit sign-off.

## Code Style Conventions

### ASCII for text the dev console renders

The dev console renders via ImGui's default font (basic Latin only), so any string that reaches it - log messages, on-screen / drawn text, and console UI labels - must be plain ASCII; em-dashes (U+2014), en-dashes (U+2013), curly quotes (U+2018/U+2019/U+201C/U+201D), and other non-ASCII glyphs render as missing-glyph boxes and clip the surrounding line. Use a plain ASCII hyphen `-` for em/en dashes and straight quotes `'` / `"` for curly ones in those runtime strings.

That rendering limit is the ONLY reason for the rule, so it does not apply to text ImGui never shows: source comments, documentation, Markdown, and tooling scripts may use non-ASCII where it helps. ASCII is a reasonable default for commit messages and PR/reply bodies but is not required there.

### No migration / history comments

The codebase has not been released. Do not leave comments describing what was renamed, what moved, what replaced what, or "this used to be X" - nobody needed the old symbol, nobody will search for it. Just delete the old thing and write the new thing.

**Bad:**
```cpp
// === Console ===
// Port contract moved to a dedicated header (src/contract/game/Console.h)
// and implementation file (src/backends/lod114d/game/Console.cpp). Not redeclared here.
```

**Bad:**
```cpp
// Replaces the previous low-level game::SendKeyPress(msg, key, extra).
void SendKey(uint32_t key);
```

**Good:** no comment at all (or a comment that describes what the current thing *is*, not what it *used to be*).

The same applies to the doc strings: don't mention that `Message` "was previously called X" or that `SplitByColor` "used to live in framework/components/". Current behavior only.

### Comment only when it adds meaning

Add a comment only when it explains something the code cannot say on its own - a non-obvious *why*, a subtle invariant, a gotcha, a pointer to an external source. A comment that restates what the code plainly does is noise.

In particular, when asked to do X, do not add or rewrite comments that narrate X. That the change implements the request is obvious from the code and the diff; a comment announcing it just outlives the request and clutters the next reader's view. Comment for the next reader, not for the task you were handed.

**Bad** (asked to "shift the banner left and show the available version" - the comments narrate exactly the request):
```cpp
// Shift the version banner left to make room for the notice.
const game::Point bannerPos{.x = noticePos.x - spaceWidth - bannerWidth, .y = baselineY};
// Draw the available-version notice on the right.
game::DrawGameText(noticeText, noticePos, NOTICE_COLOR, BANNER_FONT);
```

**Good** (no comment where the code speaks for itself; a comment only where it earns its place):
```cpp
// D2 draws text up from the baseline, so the last row keeps glyphs on-screen.
const int32_t baselineY = static_cast<int32_t>(screen.height) - 1;
```

### Redundant qualifiers, casts, access specifiers, and includes

Write the minimal form. Cleanups (including ReSharper) routinely strip these, so
writing them minimal up front makes a cleanup a no-op rather than a diff.

- **Namespace qualifiers.** Qualify only as far as name lookup needs from the
  current scope. Inside `namespace d2bs::api`, write `game::Unit`, not
  `d2bs::game::Unit`; inside a member of `d2bs::game::Control`, write `FromPtr`,
  not `Control::FromPtr`. Reach for a fully-qualified name only to disambiguate.
- **Casts.** Drop a `static_cast<T>(x)` when `x` is already a `T` or converts to
  one implicitly without narrowing - `DWORD n = request.body.size();`, not
  `static_cast<DWORD>(request.body.size())`; a single cast, not nested casts that
  funnel the same value through (`static_cast<unsigned char>(*p)` when assigning to
  a `wchar_t`, not `static_cast<wchar_t>(static_cast<unsigned char>(*p))`). Keep a
  cast that marks a real narrowing or a signed/unsigned flip the reader should see,
  or that a clang-tidy check requires - e.g. `static_cast<void*>` on a multi-level
  pointer (`const uint8_t**`) passed to `memcpy`, which
  `bugprone-multi-level-implicit-pointer-conversion` rejects when left implicit.
  Such a cast looks redundant to the compiler and to ReSharper but is not; guard it
  against re-removal with `// ReSharper disable once CppRedundantCastExpression`.
- **Access specifiers.** Drop a redundant access label: a leading `private:` in a
  `class` (already private) or `public:` in a `struct` (already public), and any
  later label that repeats the current access.
- **Includes.** Remove genuinely unused includes - but an include that is needed
  for macros, linkage, an inline definition, or a complete type yet names no symbol
  in the file is *load-bearing*: keep it and guard it with
  `// ReSharper disable once CppUnusedIncludeDirective`. See "Load-bearing includes"
  under Include Path Strategy.

### Naming

- **Methods/Functions**: PascalCase (e.g., `Start()`, `GetState()`, `RemoveAllForIsolate()`)
- **Local variables**: camelCase (e.g., `stackTrace`, `threadId`, `codePage`)
- **Function parameters**: camelCase (e.g., `threadId`, `codePage`)
- **Private class members**: trailing underscore (e.g., `path_`, `mutex_`, `isolate_`)
- **Public struct members**: no underscore (e.g., `x`, `width`, `isVisible`)
- **Singletons**: `Instance()` method (e.g., `ScriptEngine::Instance()`)
- **Boolean variables**: `isX` or `hasX` prefix (e.g., `isOpen`, `hasRow`, `isConnected`)
- **Constants**: UPPER_SNAKE_CASE for macros and constexpr (e.g., `D2BS_VERSION`)

#### Method prefix convention

Game-layer handle methods follow a three-bucket prefix convention that signals *what kind of operation* the method performs:

- **(bare)**: identity lookups, static factories, data reads - `Unit::Find(id)`, `Unit::Player()`, `Pos()`, `Name()`
- **`Get`**: iteration primitives, bulk collects, and resolved-reference reads (dispatch internally then resolve to a handle) - `GetFirstItem()`, `GetNextInGame()`, `GetItems()`, `GetPresetUnits(...)`, `GetOwner()`
- **`Find`**: filtered search / composed walk - `FindFirst`, `FindNext`, `FindInvItem`, `FindMerc`, `FindRoomAt`

### Integer Types

Use sized integer types from `<cstdint>` instead of unsized types:
- Use `int32_t`, `int64_t` instead of `int`, `long`
- Use `uint32_t`, `uint64_t` instead of `unsigned int`, `unsigned long`
- Use `size_t` for sizes and indices when interfacing with STL
- Exceptions: V8 API callbacks may require `int` in their signatures

### Arrays

Use `std::array` instead of C-style arrays. This applies everywhere including game structs in `src/backends/lod114d/imports/extras/` - `std::array` has identical memory layout to C arrays (`sizeof(std::array<T,N>) == sizeof(T[N])`) so binary compatibility is preserved. All struct sizes are verified by `static_assert`.

### Globals and Static Members

Use `inline` for any variable defined in a header to avoid ODR violations across translation units. No performance penalty - `inline` is purely a linkage directive:
```cpp
// Class static members - inline static in the header
class Foo {
    inline static std::jthread thread_;
    inline static int32_t counter_ = 0;
};

// Non-member globals in headers - inline prevents duplicate symbols
inline uint32_t* Ping = nullptr;
```

### V8 Value Creation

Prefer `api::v8_convert::ToV8(isolate, value)` over direct V8 factory calls (`v8::String::NewFromUtf8`, `v8::Integer::New`, `v8::Number::New`, `v8::Boolean::New`). `ToV8` provides consistent error handling and supports: `const char*`, `std::string`, `std::string_view`, `std::filesystem::path`, `int32_t`, `uint32_t`, `double`, `bool`. Also has overloads for `Point` / `Position` / `Size` that emit `{x, y}` / `{width, height}` v8 objects.

### Geometric Primitives

The shared 2D types live in `src/contract/game/Types.h`:
- `Point { int32_t x, y; }` - signed; map / pathfinder / drawing coords
- `Position { uint32_t x, y; }` - unsigned; game grid / world coords
- `Size { uint32_t width, height; }` - unsigned dimensions
- `Rect { Position origin; Size size; }` - rectangle with `Contains(Point/Position)` overloads

All four default-construct to zero (default member initializers) and provide `operator==` (defaulted).

**Rectangle-shaped game types (`Room`, `Level`, `Control`) expose a single `Bounds()` accessor** returning `Rect`, not separate `Pos()` / `Size()` methods. One resolve per call, and callers use `r.origin` / `r.size` directly (no `Rect::Pos()` wrapper). Point-shaped types (`Unit`, `Party`) keep `Pos()`.

**Use the `::Zero` constants for explicit zero values** - clearer than `Point{}` / `{.x = 0, .y = 0}`:
```cpp
// Good
auto p = v8_extract::Point(args, 0).value_or(d2bs::game::Point::Zero);
return d2bs::game::Position::Zero;

// Avoid
auto p = v8_extract::Point(args, 0).value_or(d2bs::game::Point{});
return {.x = 0, .y = 0};
```

**Use the operators for arithmetic** - `Point` has `operator+/-`; `Position` has `operator+/-` (Point is signed, Position is unsigned - caller responsible for not underflowing Position):
```cpp
// Good
auto offset = roomPos - grid.rect.origin;
Point np = cur + dir;

// Avoid
auto offsetX = roomPos.x - grid.rect.origin.x;
auto offsetY = roomPos.y - grid.rect.origin.y;
Point np{.x = cur.x + dir.x, .y = cur.y + dir.y};
```

**Use the conversion methods** between Point and Position rather than manual `static_cast` pairs:
```cpp
// Good
auto p = unit.Pos().ToPoint();              // Position -> Point (signed)
auto pos = p.ToPosition();                  // Point -> Position (after non-negative guard)

// Avoid
Point p{.x = static_cast<int32_t>(unit.Pos().x), .y = static_cast<int32_t>(unit.Pos().y)};
Position pos{.x = static_cast<uint32_t>(p.x), .y = static_cast<uint32_t>(p.y)};
```

**Use `Size::Area()` for `width * height`** - handles the cast to `size_t` correctly:
```cpp
// Good
data.resize(size.Area(), fill);

// Avoid
data.resize(static_cast<size_t>(size.width) * size.height, fill);
```

### V8 Class Bindings

All classes use `V8ClassBase<T>` CRTP pattern with inline lambdas:

```cpp
class JSExample : public V8ClassBase<JSExample, ExampleData> {
public:
    static constexpr std::string_view ClassName = "Example";
    V8_CLASS_NOT_CONSTRUCTABLE(Example)  // or provide New()
    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};
```

**Method/Property registration** uses `+[]` lambdas:
```cpp
Method(isolate, proto, "methodName", +[](const v8::FunctionCallbackInfo<v8::Value>& args) { ... });
Property(isolate, inst, "propName", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) { ... });
```

### Class Ownership Model

| Category | Classes | Uses MakeWeak | Constructor |
|----------|---------|---------------|-------------|
| Game Data | Room, Party, Unit, Area, Exit, PresetUnit, Control | YES | NOT_CONSTRUCTABLE |
| Script-Owned | File, FileTools, Directory, SQLite, DBStatement, Profile, Socket, HttpClient, Sandbox | YES | Factory/CONSTRUCTABLE |
| External Managed | Script | NO | NOT_CONSTRUCTABLE |
| Screen Hooks | Frame, Box, Line, Text, Image | Owned by DrawableRegistry | CONSTRUCTABLE |

### Game Thread Safety

Game types are identity-based handles with per-frame pointer caching (`HandleCache`). See `docs/game_thread_safety.md` for full details.

- **GameReadLock** (in `ResolvePtr()`) - automatic, per-resolve. Scripts never block each other.
- **Bridge::Lock()** - explicit, for V8 callbacks that iterate game data or do multi-step traversals. Place as the first line of the callback.
- **GameWriteLock** - game thread only. Held continuously across the frame body; released during `GameLoop::OnSleep`'s drain loop in 1ms slices so script readers can acquire `GameReadLock`, then reacquired before returning to the game's frame work. Bootstrap via `firstSleep_` first-tick handling.
- **GameThread::Execute()** - post work to game thread from scripts. For menu operations requiring game thread (login, createGame, etc.).

When implementing stubs: simple property reads just work (ResolvePtr handles locking). Iterating game linked lists or mutating game state needs `Bridge::Lock()`. Menu UI operations need `GameThread::Execute()`.

### Game Abstraction Patterns

**JS API -> Game Layer delegation:**
```cpp
// Property getter pattern
auto* myUnit = Unwrap(info.Holder());
if (!myUnit) return;
auto unit = myUnit->Resolve();
if (!unit) return;
info.GetReturnValue().Set(unit.Pos().x);

// Method with WaitForGameReady
if (!d2bs::game::WaitForGameReady()) {
    v8_exception::ThrowError(args.GetIsolate(), "Game not ready");
    return;
}

// Control methods require menu state
if (d2bs::game::GetGameState() != d2bs::game::GameState::Menu) return;
```

**MyUnit::Resolve() sentinel for `me` object:**
```cpp
// unitId=0, type=0 resolves to the current player unit
if (unitId == 0 && type == 0) return d2bs::game::Unit::Player();
```

**PrivateType values** (match reference bitmask: `PRIVATE_ITEM & PRIVATE_UNIT == PRIVATE_UNIT`):
- `PrivateType::Unit = 0x01` - regular units from `getUnit()` and `getItems()`
- `PrivateType::Item = 0x03` - inventory items from `getItem()` (uses `InvUnit` with owner info)

**Where search/filter logic lives:**

The game layer owns only **identity lookups** (`Find(id)`, `Level::Get(no)`, etc.), **iteration primitives** (`GetFirstInGame(type)` / `GetNextInGame()`, `GetFirstItem()` / `GetNextItem()`, `GetFirst()` / `GetNext()` on Room/Party/Control, etc.), **per-handle game-memory reads** (`Pos()`, `Name()`, etc.), and the small number of game-specific dispatchers that can't be expressed as primitive composition (e.g. `Unit::GetOwner()` which branches on `Type()` between `GetMonsterOwner` and the dwOwnerId field). Any operation that composes primitives + match logic (filtered searches, cross-entity walks, bulk collects) lives in `src/contract/game/Finders.h` as inline method definitions on the relevant handle class. Port authors reimplement primitives, accessors, and dispatchers only; filter logic is shared across all game versions.

**Import type alignment (cast elimination):**

When you see narrowing/widening `static_cast` at call sites of `imports::*` functions, suspect the import declaration has the wrong type - usually `uint32_t` where the game actually uses `int32_t` / `uint16_t` / a real struct pointer. Source of truth for 1.14d signatures is `reference/d2bs/D2Ptrs.h` (FUNCPTR / VARPTR macros); D2MOO's typed structs in `dependencies/D2MOO/source/` are the source for pointer types. Fix the import declaration first, then drop the casts. Width-change casts (`uint32_t` <-> `uint16_t`) need verification that no caller relies on the upper bits; same-bit-pattern signedness flips are safer.

Prefer narrow fixes (just the import + d2bs callers) over wide ones (touching the framework `game/*.h` contract). The framework game contract is consumed by every port and by JS bindings - changing it has wide blast radius. Only widen the fix when the JS API surface already wants the more-specific type AND no port works around the current one.

**Bridge initialization contract:**

`game::Bridge::Init()` returns `bool`. The d2bs port's implementation pops a `MessageBoxW(MB_OK | MB_ICONERROR, "d2bsng init failure", ...)` at each distinct failure point (module-base resolution, import registry mismatch, asm-thunk resolve, hook install, per-import probes) and returns `false`. `DllMain`'s `DLL_PROCESS_ATTACH` returns `FALSE` on Init failure to abort DLL load. New version ports MUST keep this shape - silent failures lead to "the bot stopped working" with no diagnostic.

**Port-chosen message sink:**

`d2bs::game::console::OnMessage` is the port-chosen sink. The backend routes to the frontend console window through the `onConsoleMessage` callback in the `GameCallbacks` table (the frontend registers it to `d2bs::js::console::OnMessage`); the symmetric `onConsoleDrawFrame` callback lets the port console host render the frontend panels. Routing through callbacks keeps the backend free of any frontend dependency - the port still chooses the sink, it just resolves it via `hooks::GetActiveCallbacks()` instead of including the frontend directly. See `src/backends/lod114d/game/Console.cpp`.

**JS API stability:**

The JS-visible surface lives in `src/frontends/js/api/` (`globals/*.cpp` for free functions, `classes/*.cpp` for V8-exposed classes). When refactoring C++ helpers consumed by these adapters (e.g. changing `GetTradeInfo` from a struct return to `std::optional<std::string>`), audit the adapter for any output-shape change. Internal C++ evolution (struct -> optional, int -> bool, helper inlining) is fine; observable JS behavior changes are not, unless explicitly approved. When in doubt, `git diff -- src/frontends/js/api/` after a refactor and read every JS-visible Set/SetNull/SetReturnValue path.

### Native Data Structs

Each V8 class has a corresponding `*Data` struct:

```cpp
struct SQLiteData {
    sqlite3* handle = nullptr;
    std::filesystem::path path;
    bool isOpen = false;
    void Close();      // Idempotent cleanup
    ~SQLiteData() { Close(); }
};
```

### Global Functions

Organized by category in `src/frontends/js/api/globals/`:
- `CoreFunctions.cpp` - print, delay, getTickCount, include, load, sendPacket, sendClick, setSpeed/getSpeed, etc.
- `GameFunctions.cpp` - getUnit, getPath, getRoom, clickMap, acceptTrade, etc.
- `MenuFunctions.cpp` - login, createGame, joinGame, getLocation, timers, events
- `HashFunctions.cpp` - md5, sha1, sha256, sha384, sha512 (with file variants)
- `Constants.cpp/h` - JS-visible constants (FILE_READ, FILE_WRITE, FILE_APPEND, ProfileType)
- `TxtTables.h` / `TxtLookup.h` - generated Diablo II .txt table/column schema (data behind getBaseStat)

## Style Enforcement

### clang-tidy

The `.clang-tidy` file starts with `Checks: '*'` (all checks enabled) and selectively disables checks. Disabled checks are documented inline in the `.clang-tidy` file.

```powershell
.\build.ps1 lint     # clang-tidy analysis
.\build.ps1 fix      # Auto-fix violations
```

### clang-format

Code formatting enforced via `.clang-format` (Google style, 120 column limit, 4-space indent):

```powershell
.\build.ps1 format        # Format all source files
.\build.ps1 check-format  # Check without modifying
```

Pre-commit hooks check formatting. Install: `git config core.hooksPath .githooks`

**Enforced naming conventions** (treated as errors via `WarningsAsErrors`):
- Functions/Methods: PascalCase
- Variables/Parameters: camelCase
- Private members: camelCase with trailing underscore
- Constants/Macros: UPPER_SNAKE_CASE
- Classes/Structs/Enums: PascalCase

### Common clang-tidy Pitfalls

All clang-tidy checks are enabled (`Checks: '*'`) with `WarningsAsErrors`. Code that compiles fine will still fail lint. Watch for these:

**`constexpr` variables must be UPPER_SNAKE_CASE** - This applies to ALL constexpr variables, including local ones inside functions. clang-tidy enforces this strictly:
```cpp
// BAD: constexpr int32_t maxRetries = 5;
// GOOD: constexpr int32_t MAX_RETRIES = 5;
```

**Designated initializers** - All aggregate types must use designated initializers:
```cpp
// BAD: {5, 10} or Point{5, 10}
// GOOD:
Point{.x = 5, .y = 10}
return {.width = 0, .height = 0};
FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 45, .y = 45}, ...);
```

**No C-style arrays** - Use `std::array` for all local/member arrays:
```cpp
// BAD:
v8::Local<v8::Value> argv[] = {arg1, arg2};
func->Call(context, recv, 2, argv);
// GOOD:
std::array<v8::Local<v8::Value>, 2> argv = {arg1, arg2};
func->Call(context, recv, argv.size(), argv.data());
```

**`unique_ptr` with forward-declared types** - If a header forward-declares a type and holds it in `std::unique_ptr`, the destructor must be defined in the `.cpp` file where the type is complete:
```cpp
// Header: forward decl + unique_ptr
class Foo;
struct Bar {
    ~Bar();  // declared here
    std::unique_ptr<Foo> ptr;
};
// .cpp: include full definition + default destructor
#include "Foo.h"
Bar::~Bar() = default;
```

**Third-party macro casts** - Macros like `SQLITE_TRANSIENT` contain C-style casts that trigger `cppcoreguidelines-pro-type-cstyle-cast`. Suppress with NOLINT:
```cpp
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
sqlite3_bind_text(handle, col, str.c_str(), len, SQLITE_TRANSIENT);
```

**`const_cast` in test fakes** - Test fakes that cast const test data to match non-const game API signatures need NOLINT:
```cpp
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - test fakes cast const test data to match game API
return Room(const_cast<void*>(static_cast<const void*>(ptr)));
```

**Container data access** - Use `.data()` instead of `&container[0]`:
```cpp
// BAD: &vec[0]
// GOOD: vec.data()
```

**Non-constant array subscripts** - `cppcoreguidelines-pro-bounds-constant-array-index` fires on `std::array` access with runtime indices. Use NOLINTBEGIN/END blocks for unavoidable cases (e.g., algorithm loops):
```cpp
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
auto val = area[ai][aj] | area[ai + 1][aj];
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
```

**Math operator precedence** - `readability-math-missing-parentheses` requires explicit parentheses around mixed `*` and `+`:
```cpp
// BAD: y * width + x
// GOOD: (y * width) + x
```

**Union access disabled globally** - `cppcoreguidelines-pro-type-union-access` is disabled in `.clang-tidy`. D2 / D2MOO game structs use C unions for memory layout (e.g. `D2DrlgRoomStrc::pMaze` / `pOutdoor` share a slot), and we can't refactor the ABI to `std::variant`. Don't re-enable the check or NOLINT every union access - the global disable is the right answer for this codebase.

**Optional access after doctest `REQUIRE`** - clang-tidy's `bugprone-unchecked-optional-access` doesn't understand that `REQUIRE(opt.has_value())` guarantees the optional is valid. Use NOLINT blocks:
```cpp
REQUIRE(loaded.has_value());
// NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
CHECK(loaded->field == expected);
// NOLINTEND(bugprone-unchecked-optional-access)
```

**Multi-level pointer to `void*` needs an explicit cast** - `bugprone-multi-level-implicit-pointer-conversion` flags passing a pointer-to-pointer (e.g. `const uint8_t**`) straight to a `void*` parameter such as `memcpy`'s. The implicit conversion is legal C++, so both the compiler and ReSharper see the `static_cast<void*>` as redundant - but clang-tidy requires it. Keep the cast and guard it:
```cpp
// ReSharper disable once CppRedundantCastExpression - clang-tidy needs the explicit void*
std::memcpy(static_cast<void*>(outBase), src, sizeof(*outBase));
```
