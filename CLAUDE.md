# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

d2bsng (D2 Botting System Next Generation) is a Windows DLL that exposes JavaScript scripting capabilities for Diablo II automation. The JS frontend is written against [unibind](https://github.com/ResurrectedTrader/unibind) (namespace `ub`), an engine-neutral embedding API, and names no engine; the engine is chosen at the glue link, so the same frontend ships as two DLLs - one on Google's V8 (the default) and one on Mozilla's SpiderMonkey. The JS API is implemented against Diablo II 1.14d (LoD): the game layer reads live game state through a typed imports layer built on D2MOO. Versions start at 2.x (legacy d2bs topped out at 1.6.x) so scripts can tell the two apart.

## Documentation

Design docs live in `docs/`. Read the one(s) covering whatever you are about to touch before changing it, and update them in the same change rather than duplicating their content here:

- `docs/coords.md` - the two D2 coordinate spaces (subtiles vs game coords) and where the subtile -> game-coord conversion lives (game-layer stubs only).
- `docs/game_thread_safety.md` - identity-based handles, per-frame pointer caching (`HandleCache`), and the game read/write lock model.
- `docs/window_message_handling.md` - the `WH_GETMESSAGE` input hook (block / dispatch / injected-input tagging), the game-window WndProc subclasses, and the console raw-input summon.
- `docs/inspector.md` - the script debugger (Chrome DevTools) over `ub::Inspector`: available where the engine has an inspector (V8, not SpiderMonkey - `ub::Inspector::Supported()`), the ixwebsocket transport, the InspectorServer / InspectorTarget / ScriptInspector split, the inbound-queue threading model, and releasing game locks during a breakpoint pause.
- `docs/compatibility.md` - the scripting compatibility-flag system: the `CompatibilityFlags` registry, the framework flag catalog, the `game::GetCompatibilityFlags()` extension point, the `Compatibility` JS object, the per-flag gating sites, and why the BOM strip and `delay` wrapper stay un-flagged.
- `docs/realms.md` - custom Battle.net gateways: the `RealmRegistry` store, the `-realm` launch option, in-memory injection into D2's gateway list by detouring the Storm registry read/write helpers (no registry persistence), realm enumeration (`game::GetRealms`) exposed as the global `getRealms()`.
- `docs/stash_tabs.md` - stash tabs: the game-agnostic `game::StashTab` identity handle (`game/StashTab.h`; backend-implemented `Type` / `Name` / `Gold` / `GetItems` / `Click` / `MoveGold`, `game::GetStashTabs`, `Unit::StashTab`), exposed as the `StashTab` JS class via `getStashTabs()` and `Unit.stashTab` (with `depositGold` / `withdrawGold` wrapping `MoveGold`), the per-tab gold attribution rule, why there is no "select tab", the character-state `pages` feed, and the `GoldActionMode` dialog codes.
- `docs/plugy_stash.md` - PlugY's multi-page stash on 1.14d, the backend behind `docs/stash_tabs.md` when PlugY is present: how PlugY pages the stash (only the shown page lives in the inventory; the rest hang off a `PYPlayerData` tail appended to the player data, parked items are stored-mode with no parent inventory), the version-allowlisted detection in `backends/lod114d/game/PlugY.cpp`, running PlugY's `Init` from `Bridge::Init` when PlugY.dll was injected without PlugY.exe, the patch-site overlap check against d2bs's hooks, the blocking swap-in / click / ack-wait / swap-back sequence over PlugY's 0x3A channel, the shared gold pool, and how to remove the PlugY code again.
- `docs/analytics.md` - anonymous usage analytics (Aptabase): the `Analytics` component, the `session_start` / `profile_active` events (the latter carrying a per-install `profileHash`, never the profile name) and exactly what they do / do not collect, the compile-time app key (build-time only; the project key is the committed default in `Directory.Build.props`, overridable per-build via `-p:D2bsAnalyticsKey` / gitignored `d2bs.local.props` / `D2BS_ANALYTICS_KEY`, and an empty value compiles analytics out), the per-launch opt-out (`-noanalytics`/`D2BS_ANALYTICS_DISABLE`), the backend-agnostic `features` tag list via `game::GetActiveFeatures()`, the anonymous `installId` (derived per launch, never stored), and the `game::GetAnalyticsLaunchOptions()` / `game::GetBackendVersion()` contract accessors.

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
#   deps           - Download unibind, V8 and SpiderMonkey without building (so an editor can
#                    resolve includes in a fresh clone); with -Platform x64 only unibind
#
# Switches:
#   -Platform Win32|x64 - Win32 (default) builds everything, including one d2bs.dll per engine:
#                    Release\js-v8-lod114d\d2bs.dll (V8) and Release\js-sm-lod114d\d2bs.dll
#                    (SpiderMonkey); x64 builds the engine- and version-free libraries into x64\Release\
#   -NoProfiling   - Compile the profiling counters (utils/Profiling.h) and the console's
#                    Profiling panel out (MSBuild -p:D2bsProfiling=false; see Directory.Build.props)
#   -Version / -AnalyticsKey - baked into the DLLs (CI passes both; see docs/analytics.md)
```

The build script auto-detects the Visual Studio installation via vswhere and works from any directory. You can also build directly with MSBuild or in Visual Studio.

## Tests

The test project (`tests/frontends/runtime/js_tests.vcxproj`) is a standalone console exe using [doctest](https://github.com/doctest/doctest). It compiles the real `src/navigation/Pathfinder.cpp` with fake game layer implementations (no DLL, no unibind, no engine, no game), plus a few other engine-free sources (`GameLoop.cpp`, `Commands.cpp`, the config and profile code) for their own tests. Tests do not link any static libs - they compile sources directly alongside test fakes.

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

Mostly the **pathfinding engine** (`src/navigation/`), plus smaller suites for launch-option parsing (`config/`), logging (`utils/`), profiles (`profile/`), the game lock (`game/`) and the game loop (`gameloop/`). The pathfinding coverage:

- **Unit tests** (`tests/frontends/runtime/pathfinding/tests/`): CollisionLookup queries, walk A*, teleport A*, walk reduction, point mutation, edge cases, penalty avoidance
- **Reference comparison** (`tests/frontends/runtime/pathfinding/tests/comparisons/`): exact-match and cost-equivalence tests comparing our A* output against an adapted reference A* implementation on identical collision grids
- **Benchmarks**: synthetic 2000x2000 grids and real game collision data (`.d2col` fixtures in `tests/frontends/runtime/fixtures/maps/`) comparing walk and teleport modes against the reference
- **Fixture system** (`tests/frontends/runtime/fixtures/`): binary `.d2col` loader for collision grids dumped from the game via `tools/dump_collision.js`

### Adding real game data for benchmarks

1. Load `tools/dump_collision.js` in d2bs while in-game - dumps `.d2col` files per level
2. Copy `.d2col` files to `tests/frontends/runtime/fixtures/maps/`
3. Run `.\build.ps1 test` - real-world benchmarks auto-discover and use them

**Requirements**: Visual Studio 2022 with ClangCL toolchain, Windows SDK 10.0.26100.0

## API Documentation

The script-visible JS API is documented by a generator pipeline under `scripts/`. The bindings in `src/frontends/runtime/api/` carry structured `///` doc comments (`@description`, `@signature`, `@param`, `@returns`, `@throws`, `@callback`, `@event`, `@type`, `@mode`); their exact vocabulary lives at the top of `extract_api.py`. The `{type}` fields may be object literals / unions / generics (`{x:number,y:number}`, `Unit|null`, `Array<{x:number}>`) - the parser is brace-balanced.

**Enumerations as option sets**: a `{type}` that names a C++ `enum class` (libclang-parsed from `game/Types.h` / `game/Constants.h`) or a constant namespace (e.g. `ProfileType`) is rendered as a value table, not prose. `extract_api.py` reads the enumerators with libclang - plus the compatibility flags from `CompatibilityFlags::RegisterDefaults()`'s documented `Register("name")` calls - into an `enums` map (only sets referenced by some `{type}` are emitted); `gen_api_docs.py` renders an Enums section and auto-links the type; `gen_dts.py` emits a literal-union alias (`type Difficulty = 0 | 1 | 2 | 3`; bitfields and constant namespaces map to `number`). A bitfield enum is typed `number` (a value is an OR-combination) by marking it `/// @flags`. So document an enum-valued member as `@type {Difficulty}` and keep the values in the C++ enum, not in the description.

| Script | Flow | Notes |
|--------|------|-------|
| `extract_api.py` | `src/frontends/runtime/api/**` -> `api.json` | Walks the bindings with **libclang**, emitting the full surface (classes, globals, constants, the `me` object, events, enums). It keys on the registration calls: `Property` / `Method` / `StaticMethod` inside a class's `Configure`, the class's `ClassName` and `New` (constructability), `function::Register` inside `Register*Functions`, `Define` inside `RegisterConstants`, and `InstanceProperty` inside `CreateMeObject`. Needs the same unibind + vcpkg + MSVC/SDK include set the build uses, and refuses to run until unibind's headers are unpacked (`build.ps1 deps`). The only script that needs libclang. |
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

Creating a git worktree needs a couple of extra steps because of the heavy,
out-of-tree dependencies:

- `dependencies/D2MOO` is a submodule and is not populated by `git worktree
  add`. Point it at the main checkout with a directory junction (not a copy or
  re-fetch).
- `dependencies/unibind/<version>/`, `dependencies/v8/<version>/` and
  `dependencies/spidermonkey/<version>/` (the fetched archives) are gitignored,
  so they are not populated either - but the build downloads what it needs, so
  a worktree builds without doing anything. Junctioning them to the main
  checkout is worth it anyway: it reuses archives that are already on disk
  instead of pulling well over a gigabyte per worktree.

Recipe (PowerShell, from the main checkout root):

```powershell
$main = (Get-Location).Path
$wt   = "$main\.claude\worktrees\<name>"
git worktree add -b <branch> $wt <base-ref>
$props = [xml](Get-Content "$main\Directory.Build.props")
# Fetched archives (gitignored) - junction each version directory from main to skip the downloads.
# name under dependencies\ -> the Directory.Build.props property holding its version
$deps = @{ unibind = 'UnibindVersion'; v8 = 'V8Version'; spidermonkey = 'SpiderMonkeyVersion' }
foreach ($name in $deps.Keys) {
    $ver = $props.SelectSingleNode("//$($deps[$name])").InnerText.Trim()
    if (Test-Path "$main\dependencies\$name\$ver") {
        New-Item -ItemType Directory -Force "$wt\dependencies\$name" | Out-Null
        New-Item -ItemType Junction -Path "$wt\dependencies\$name\$ver" -Target "$main\dependencies\$name\$ver"
    }
}
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
the junction targets and deletes the *main* checkout's fetched unibind / V8 /
SpiderMonkey trees (gitignored, slow to re-download) and D2MOO submodule
contents. A non-recursive delete removes the junction itself and leaves the
target intact - and fails harmlessly on a `dependencies\<name>\<version>` the
worktree's own build populated, which is a real directory that goes with the
tree:

```powershell
$main = (Get-Location).Path
$wt   = "$main\.claude\worktrees\<name>"
$props = [xml](Get-Content "$main\Directory.Build.props")
$deps = @{ unibind = 'UnibindVersion'; v8 = 'V8Version'; spidermonkey = 'SpiderMonkeyVersion' }
# Detach the junctions BEFORE deleting anything - the target stays intact.
foreach ($name in $deps.Keys) {
    $ver = $props.SelectSingleNode("//$($deps[$name])").InnerText.Trim()
    try { [System.IO.Directory]::Delete("$wt\dependencies\$name\$ver", $false) } catch {}
}
[System.IO.Directory]::Delete("$wt\dependencies\D2MOO", $false)
git worktree remove $wt        # --force if the tree has uncommitted changes
# git branch -D <branch>       # optional: also drop the worktree's branch
```

If `git worktree remove` refuses with "'.git' is not a .git file" - a worktree
whose internal links were written by Cygwin git (`/cygdrive/...` paths that
Git-for-Windows can't parse) - detach the junctions as above, then remove the
tree and its admin entry by hand and prune. Delete from a Cygwin / Git-Bash
shell (`rm -rf "$wt"`) if the fetched engine headers - deeply nested under an
already-long worktree prefix - overflow MAX_PATH for `Remove-Item`:

```powershell
Remove-Item -Recurse -Force $wt
Remove-Item -Recurse -Force "$main\.git\worktrees\<name>"
git worktree prune
```

Either way, confirm `dependencies\unibind\<version>`, `dependencies\v8\<version>`,
`dependencies\spidermonkey\<version>` and `dependencies\D2MOO` in the main
checkout still hold their files afterward.

## Git

Commits should not be GPG signed:
```bash
git commit --no-gpg-sign -m "message"
```

Never put a Claude Code session link (`https://claude.ai/code/session_...`, a
`Claude-Session:` trailer, or any equivalent) in a commit message, PR title or
body, review reply, or anything else that lands in the repository or on GitHub.
A `Co-Authored-By` trailer is fine; the session URL is not.

## Architecture

### Project Structure

The codebase is split into nine build targets (seven static libs + two DLLs) under `src/`, plus a test project. A frontend (JS) and a backend (1.14d) both compile against a shared `contract`, and a thin glue project links one of each - plus a JavaScript engine - into a DLL. There are two glue projects, one per engine, over the same frontend and backend. `utils`, `contract`, `core`, `navigation`, `services` and `runtime` hold no game-version-specific and no engine-specific code and build for both Win32 and x64; the backend, both glues and the tests are Win32 only (the `.slnx` maps them).

```
d2bsng/
├── build.ps1               Build entry: build / Debug / format / check-format / lint / fix / test / deps
├── Directory.Build.props   Build-wide settings: analytics key, profiling switch, LTO, and the unibind / V8 /
│                           SpiderMonkey versions + the Fetch* targets that download them
├── vcpkg.json              Single shared vcpkg manifest (all deps)
├── dependencies/
│   ├── unibind/            unibind headers + backend libs (fetched per version/arch/flavor at build time)
│   ├── v8/                 V8 headers + monolith (fetched per version/flavor at build time)
│   ├── spidermonkey/       SpiderMonkey headers + static lib (fetched per version/flavor at build time)
│   └── D2MOO/              D2MOO submodule - game struct/function reference
├── docs/                   Design docs (see "Documentation" above)
├── scripts/                Maintainer tooling (lint.ps1, fetch_archive.ps1, API extraction + docs/d.ts/site generators, table generators)
├── src/
│   ├── utils/              utils.lib - standalone utilities (crypto, threading, stackwalker, profiling counters)
│   ├── contract/           contract.lib - the boundary both frontends and backends compile against
│   │   ├── game/               Game interface headers + framework-owned utilities (only GameLock.cpp is compiled)
│   │   └── config/             Shared DTOs: ProfileData, ScriptPaths
│   ├── core/               core.lib - shared infrastructure (depends on contract)
│   │   ├── config/             AppConfig, IniConfigStore, CompatibilityFlags, Version, OptionParser (launch-option parsing, and removing those switches from the command line afterwards)
│   │   ├── detour/             Typed Detours slots + batched attach/detach transactions (every Detours hook in the tree)
│   │   ├── speedhack/          Global game-time scaling
│   │   ├── input/              WH_GETMESSAGE input hook + WM_COPYDATA subclass + injected-input tagging
│   │   ├── proxy/              SOCKS5 connect hook + bypass scope for script sockets
│   │   └── http/               WinHTTP request engine (takes the proxy bypass above)
│   ├── navigation/         navigation.lib - map algorithms over the contract; depends on contract + utils only
│   │   ├── Pathfinder.h/.cpp   A* pathfinder + collision grids (d2bs::pathfinding)
│   │   └── ExitFinder.h/.cpp   Level-exit finder + ExitInfo (d2bs::navigation)
│   ├── services/           services.lib - bot services over the contract; depends on contract + core + utils
│   │   ├── analytics/          Anonymous usage analytics (Aptabase)
│   │   ├── characterstate/     Character-state snapshot -> D2BotNG manager (WM_COPYDATA)
│   │   ├── dde/                DDE service
│   │   ├── profile/            ProfileService (profile lookup/switch logic)
│   │   └── update/             GitHub-release update checker (6h poll -> version-banner marker)
│   ├── frontends/          One directory per scripting frontend
│   │   └── runtime/        runtime.lib - JavaScript frontend over unibind (ub::); names no engine
│   │       ├── api/            Script-visible API
│   │       │   ├── core/           Binding kit: Class.h (api::ClassBase over ub::Class), Function.h (global
│   │       │   │                   functions), Convert.h / Extract.h (values <-> native), Error.h (throws,
│   │       │   │                   arg checks), InstanceTracker.h (live-wrapper counts)
│   │       │   ├── classes/        Classes: game/, io/, scripting/, drawing/ + ClassRegistry (install all, `me`)
│   │       │   └── globals/        Global functions + constants, one Register*() per category
│   │       ├── components/     Frontend internals:
│   │       │   ├── script/         Script (one isolate + thread per script), ScriptEngine, CompileSource (+ compat
│   │       │   │                   prelude), CodeCache, NativeCallHook (binding trampolines: stack capture + profiling)
│   │       │   ├── engine/         Engine::GetPlatform - the one ub::Platform, and the engine fault handler
│   │       │   ├── events/         Event system (events carry native payloads; MakeArgs builds script values)
│   │       │   ├── gameloop/       Per-frame game-thread loop + lock release
│   │       │   ├── console/        ImGui dev console (log/REPL/scripts/stacktraces/threads/profiling/settings)
│   │       │   ├── inspector/      Chrome DevTools server over ub::Inspector (does nothing on an engine without one)
│   │       │   ├── drawing/        Screen-hook drawables (Box/Frame/Line/Text/Image) + version banner
│   │       │   └── Host.h/.cpp     Frontend lifecycle (d2bs::runtime::Host) + GameCallbacks wiring
│   ├── backends/           One directory per game-version backend
│   │   └── lod114d/        lod114d.lib - 1.14d game backend (implements contract); depends on contract + core
│   │       ├── game/           1.14d implementation (.cpp + internal .h)
│   │       ├── imports/        Typed game func/var registry + 1.14d offsets + extras/ structs
│   │       ├── hooks/          Inline / IAT hooks, realm injection, intercepts
│   │       ├── asm_thunks/     Hand-written ABI thunks
│   │       └── console/        Port console host (window + GL + ImGui glue)
│   └── glue/               One directory per frontend+engine+backend combo (the shippable targets)
│       ├── js-v8-lod114d/  d2bs-v8.vcxproj -> Release\js-v8-lod114d\d2bs.dll: runtime + lod114d + ... + unibind_backend_v8 + V8
│       │   ├── dllmain.cpp     DLL entry point (Bridge::Init -> InstallAll -> runtime::Host::Initialize)
│       │   └── version.rc      DLL version resource
│       └── js-sm-lod114d/  d2bs-sm.vcxproj -> Release\js-sm-lod114d\d2bs.dll: same sources (../js-v8-lod114d/
│                           dllmain.cpp + version.rc), linked with unibind_backend_spidermonkey + SpiderMonkey
└── tests/
    └── frontends/
        └── runtime/        js_tests.exe - doctest suite (pathfinding, config, game loop, ...) + fakes
```

### Build Targets

| Target | Type | Output | Contents |
|--------|------|--------|----------|
| **utils** | Static lib | `Release/utils.lib` | `src/utils/` - crypto, threading, stackwalker |
| **contract** | Static lib | `Release/contract.lib` | `src/contract/` - game interface headers (`game/*.h`) + framework-owned utilities + shared DTOs (`config/ProfileData.h`, `config/ScriptPaths.h`). The boundary both frontends and backends compile against. Depends on utils. |
| **core** | Static lib | `Release/core.lib` | `src/core/` - shared infra: config (AppConfig/Ini/CompatibilityFlags/Version/OptionParser), detour, speedhack, input, proxy, http. Depends on contract + utils. |
| **navigation** | Static lib | `Release/navigation.lib` | `src/navigation/` - game algorithms over the contract: A* pathfinder + level-exit finder. Depends on contract + utils ONLY. Has unresolved game:: symbols. |
| **services** | Static lib | `Release/services.lib` | `src/services/` - bot services over the contract: analytics, character-state IPC, DDE, profile switching, update checks. Depends on contract + core + utils. Has unresolved game:: symbols. |
| **runtime** | Static lib | `Release/runtime.lib` | `src/frontends/runtime/` - JavaScript frontend (api/, components/). Depends on contract + core + navigation + services + utils + unibind's headers. Has unresolved game:: symbols and unresolved unibind symbols (the backend library is a glue link input). |
| **lod114d** | Static lib | `Release/lod114d.lib` | `src/backends/lod114d/` - 1.14d game backend implementing the contract (Win32 only). Depends on contract + core + utils. No frontend, unibind or engine dependency. |
| **d2bs-v8** | DLL | `Release/js-v8-lod114d/d2bs.dll` | `src/glue/js-v8-lod114d/d2bs-v8.vcxproj` - glue: DllMain + version.rc. Links runtime + lod114d + navigation + services + contract + core + utils + `unibind_backend_v8.lib` + `v8_monolith.lib`, resolves all symbols. The default (V8) build. |
| **d2bs-sm** | DLL | `Release/js-sm-lod114d/d2bs.dll` | `src/glue/js-sm-lod114d/d2bs-sm.vcxproj` - the same glue sources (it compiles `../js-v8-lod114d/dllmain.cpp` + `version.rc`) and the same libraries, linked with `unibind_backend_spidermonkey.lib` + `spidermonkey.lib` instead. `TargetName` is `d2bs`, because the loader looks for `d2bs.dll`. |
| **js_tests** | Console EXE | `Release/js_tests.exe` | `tests/frontends/runtime/` - doctest tests with fake game layer |

Each glue writes into `$(SolutionDir)$(Configuration)\<glue directory name>\`, so the two `d2bs.dll`s do not overwrite each other.

### How Linking Works

```
utils.lib       <- fully resolved, standalone
     v
contract.lib    <- game interface headers (game/*.h) + shared DTOs. Depends on utils.
     v              (declares game::Unit::Pos() etc.; impl lives in a backend)
core.lib        <- config / speedhack / proxy / http. Depends on contract + utils.
     v
     |         navigation.lib  <- A* pathfinder + exit finder. Game algorithms
     |         (map algorithms)   written against the contract, so they have
     |              v             UNRESOLVED game:: symbols too. Depends on
     |              |             contract + utils ONLY - never core, never a
     |              |             frontend, never unibind or an engine header.
     |              |
     |         services.lib    <- analytics, character-state IPC, DDE, profile
     |         (bot services)     switching, update checks. Also written against
     |              v             the contract, so also UNRESOLVED game::.
     |              |             Depends on contract + core + utils - never a
     |              |             frontend, never unibind or an engine header.
     +--------------+-------------+----------------------------+
     v                            v
runtime.lib                        lod114d.lib    <- frontend and backend are mutually blind:
(JS frontend over unibind)    (1.14d backend)   runtime has UNRESOLVED game:: symbols;
UNRESOLVED game:: and ub::    implements them    lod114d implements them and calls UP only
symbols; depends on           ; depends on       through the GameCallbacks function table
contract + core +             contract + core
navigation + services
     v                            v
     +-------------+--------------+
                   v
     +-------------+-------------------------------+
     v                                             v
js-v8-lod114d\d2bs.dll                   js-sm-lod114d\d2bs.dll
 + unibind_backend_v8.lib                 + unibind_backend_spidermonkey.lib
 + v8_monolith.lib                        + spidermonkey.lib (+ its system libs)
     <- glue: DllMain + wiring. Each links runtime + lod114d + navigation + services +
        contract + core + utils, plus ONE unibind backend and its engine, which
        resolves the ub:: symbols. LTO inlines the thin game:: wrappers across
        all libs.
```

### Game Interface vs Implementation

The game abstraction is split across two directories:

- **`src/contract/game/`** - Interface headers (18 `.h` files) plus `GameLock.cpp`, which only holds the two thread-local lock definitions that cannot be inline under LTO. Defines the wrapper classes (`Unit`, `Room`, `Level`, etc.) with method declarations using opaque `void*` pointers. Part of `contract.lib`. No game-version-specific code. Both the frontend and the backend compile against it.

- **`src/backends/lod114d/game/`** - 1.14d implementation (12 `.cpp` files + internal headers like `RoomData.h` / `DrlgHelpers.h`). Part of `lod114d.lib`. The version-specific game-function/variable bindings, structs, and hooks live alongside it under `src/backends/lod114d/imports/` (typed import registry + per-DLL declarations), `src/backends/lod114d/imports/extras/` (structs not in D2MOO), `src/backends/lod114d/asm_thunks/`, and `src/backends/lod114d/hooks/`.

To add support for a different game version: create a new backend lib (a sibling of `backends/lod114d/`) with its own `game/` implementation and vcxproj, depending on `contract` + `core` + `utils`, then a glue project under `glue/` that links it with the frontend and an engine (one glue per engine, as `js-v8-lod114d` / `js-sm-lod114d` do for 1.14d). A different JavaScript engine needs no new frontend: it needs a unibind backend library and a glue project that links it. To add a different frontend (a non-JS scripting host, or a C-ABI bridge that re-exports the contract for other languages): create a sibling of `frontends/runtime/` over the same `contract` + `core`, and reuse `navigation` (pathfinding, exit finding) and `services` (analytics, character state, DDE, profiles, updates) rather than reimplementing them. Frontend and backend never reference each other - only the glue does.

### Include Path Strategy

Each project has specific include directories that make cross-project includes work:

| Project | Include Directories |
|---------|-------------------|
| **utils** | `$(ProjectDir)` |
| **contract** | `$(ProjectDir)` ; `$(SolutionDir)src` |
| **core** | `$(ProjectDir)` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src` |
| **navigation** | `$(ProjectDir)` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src` |
| **services** | `$(ProjectDir)` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src` |
| **runtime** | `$(ProjectDir)` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src\services` ; `$(SolutionDir)src` ; `$(UnibindIncludeDir)` (also an external include dir, so its headers are system headers to the compiler and lint) |
| **lod114d** (backend) | `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src` ; `$(ProjectDir)` ; `$(ProjectDir)game` ; D2MOO include roots |
| **d2bs-v8** / **d2bs-sm** (glue DLLs) | `$(SolutionDir)src\frontends\runtime` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src\backends\lod114d` ; `$(SolutionDir)src` |
| **js_tests** | `$(ProjectDir)fakes\shims` ; `$(SolutionDir)src\contract` ; `$(SolutionDir)src\core` ; `$(SolutionDir)src\services` ; `$(SolutionDir)src\frontends\runtime` ; `$(SolutionDir)src` ; `$(ProjectDir)` |

All includes use project-root-relative paths - never use relative paths like `../`:
```cpp
#include "game/Unit.h"                    // Interface header (from contract)
#include "api/core/Class.h"               // Frontend internal (js)
#include "unibind/unibind.h"              // unibind (runtime only; never an engine header)
#include "config/AppConfig.h"             // Config (from core)
#include "config/ProfileData.h"           // Shared DTO (from contract)
#include "navigation/Pathfinder.h"        // Map algorithm (from navigation)
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
- **Macro-driven implementation.** `tests/frontends/runtime/test_main.cpp` is the one TU
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
- **contract/** (the boundary) depends on: utils + standard library only. NEVER on core, the frontend, unibind, an engine, or any backend. Holds the game interface (`game/*.h`), the framework-owned utilities (Finders/GameLock/GameThread/HandleCache/Types), and the shared DTOs (`config/ProfileData.h`, `config/ScriptPaths.h`). `game/Menu.h` includes `config/ProfileData.h` (same project) so `Login()` takes the profile struct by const-ref.
- **core/** (shared infra) depends on: contract + utils. Holds config (AppConfig/Ini/CompatibilityFlags/Version/OptionParser), detour (the Detours slot/batch API every hook in the tree goes through), speedhack, proxy (SOCKS5 hook), http (the WinHTTP request engine, which takes the proxy bypass), input (the game-window input hook). NEVER on the frontend, unibind, an engine, or a backend.
- **navigation/** (map algorithms) depends on: **contract + utils, and nothing else**. Not core, not a frontend, not `api/`, not unibind or any engine header - the `#include` list is `game/*`, `utils/*` and the standard library. It holds the derived, game-version-agnostic algorithms over the contract's primitives: the A* pathfinder (`d2bs::pathfinding`) and the level-exit finder (`d2bs::navigation::GetExits` + `ExitInfo`). That narrow dependency set IS the library's purpose: these are algorithms every frontend wants and no backend needs, so they belong to neither. Anything that needs a setting, a log sink, a script callback or an engine value is a frontend concern and stays in the frontend. Like a frontend, it leaves `game::` symbols unresolved until the glue link.
- **services/** (bot services) depends on: contract + core + utils. Holds the bot's own background features - anonymous analytics, the character-state snapshot sent to the D2BotNG manager, the DDE service, profile lookup/switching, and the GitHub-release update checker. None of them is frame-driven or script-driven, and none names a script value, so they belong to no frontend: a second frontend links this library rather than reimplementing it. Like a frontend, it leaves `game::` symbols unresolved until the glue link. NEVER on a frontend, `api/`, unibind, an engine, or a backend.
- **frontends/runtime/** (JavaScript frontend) depends on: contract + core + navigation + services + utils + unibind's headers. **Never an engine header** - no `v8.h`, no `jsapi.h`, nothing from `dependencies/v8` or `dependencies/spidermonkey`; every script value is a `ub::` type, and which engine answers is decided by the glue's link. Reaches the game only through `game::` contract symbols (resolved at the glue link) and pushes its hooks down through the `GameCallbacks` table; it NEVER references a concrete backend.
  - **frontends/runtime/api/** depends on: contract (game/ interface + DTOs), core, navigation, services, components/, utils/, unibind.
  - **frontends/runtime/components/** depends on: contract, core, navigation, services, utils/, unibind. Exceptions that reach into `api/`: `components/script/Script.cpp` is the JS-API composition root (it sets up the isolate and registers the `api/` ClassRegistry + globals + constants, and reads the InstanceTracker), and `components/events/Events.h` builds event arguments with `api::convert`. The rule every component keeps: a script value (`ub::Local` / `ub::Global`) is made, held and released only on its script's thread, so the game thread never holds one - events carry native payloads and build their arguments in `MakeArgs` on the script thread, and drawables keep their handlers on the owning `Script`.
  - Engine differences are answered where unibind answers them, never by naming the engine: `ub::Inspector::Supported()` (only V8 has an inspector), the optional fields of `ub::HeapStatistics`, and `ub::Platform::BackendName()` / `BackendVersion()` for display only. An operation one engine lacks is a link error in that engine's glue, not a runtime branch.
- **backends/lod114d/** (1.14d backend) depends on: contract + core + utils, plus sibling port headers (imports/, hooks/, asm_thunks/). Implements the `game::` contract symbols and calls UP into the frontend ONLY through the `GameCallbacks` pointers it is handed at init (`hooks::GetActiveCallbacks()`). NEVER on the frontend, api/, unibind, or an engine. Config reads go through `core`; console output and rendering go through the `onConsoleMessage` / `onConsoleDrawFrame` callbacks.
- **glue/js-v8-lod114d/** and **glue/js-sm-lod114d/** (glue) depend on: runtime + lod114d + navigation + services + contract + core + utils, plus one unibind backend library and its engine as link inputs. The only projects that see both a frontend and a backend; they own `DllMain` and the bring-up wiring (one shared `dllmain.cpp` / `version.rc`, in `js-v8-lod114d/`).

### Key Design Decisions

- **Platform**: the 1.14d backend and both `d2bs.dll`s are Win32, because the game is 32-bit (and the engine archives are x86 only). `utils`, `contract`, `core`, `navigation`, `services` and `runtime` carry no game-version-specific or engine-specific code and build for both Win32 and x64, so a 64-bit target can reuse them (unibind's archive is fetched per architecture, `UnibindArch` following `$(Platform)`); the test project is Win32.
- **One frontend, engine chosen at link time**: the frontend compiles once against unibind; the V8 and SpiderMonkey DLLs differ only in link inputs.
- **ClangCL compiler**: Uses LLVM/Clang with MSVC compatibility
- **Static linking**: VCPKG dependencies and CRT statically linked (MT/MTd runtime)
- **C++23**: Uses latest C++ standard
- **LTO enabled**: the projects set `WholeProgramOptimization=true` in Release, and `Directory.Build.props` turns that into `-flto` for clang-cl (the ClangCL toolset ignores the property on its own), so objects are bitcode and lld-link optimises the whole program - wrapper methods inline across TUs and static libs.
- **Parallel builds**: `Directory.Build.props` sets `MultiProcessorCompilation` for every project and `build.ps1` passes MSBuild `-m`, so sources compile across all cores and independent projects (lod114d, js, js_tests, the two glues) build concurrently; `EnforceProcessCountAcrossBuilds` caps the total compiler process count at the core count.

### Dependencies (via VCPKG)

Single `vcpkg.json` at solution root, shared by all main projects:
- spdlog + fmt: Logging
- stackwalker: Stack traces (DbgHelp-backed; reads PDB symbols)
- sqlite3: Database support
- detours: Microsoft Detours - function hooking
- imgui (opengl2 + win32 bindings): dev console UI
- ixwebsocket (no default features, so no TLS): the Chrome DevTools transport
- magic-enum, nlohmann-json

The test project (`tests/frontends/runtime/`) has its own `vcpkg.json` with just `doctest` (header-only test framework).

### Fetched archives: unibind, V8, SpiderMonkey

unibind and the two engines are not vcpkg dependencies, and none of them is in the repository. Each is one published GitHub-release archive, downloaded by a target in `Directory.Build.props` through `scripts/fetch_archive.ps1`:

| Target | Opt-in property (set by) | Unpacks into | Holds |
|--------|--------------------------|--------------|-------|
| `FetchUnibind` | `UnibindRequired` (runtime, d2bs-v8, d2bs-sm) | `dependencies/unibind/$(UnibindVersion)/$(UnibindArch)-$(EngineFlavor)/` | `include/unibind/*.h`, `lib/unibind_backend_v8.lib`, `lib/unibind_backend_spidermonkey.lib` (checked against the published `.sha256`) |
| `FetchV8` | `V8Required` (d2bs-v8) | `dependencies/v8/$(V8Version)/x86-$(EngineFlavor)/` | V8 headers + `v8_monolith.lib` |
| `FetchSpiderMonkey` | `SpiderMonkeyRequired` (d2bs-sm) | `dependencies/spidermonkey/$(SpiderMonkeyVersion)/x86-$(EngineFlavor)/` | SpiderMonkey headers + `spidermonkey.lib` |

Each runs before anything compiles or links against it, only when its directory is absent, and only for the configuration being built. The directory is the unit: the archive is extracted to a staging sibling and renamed into place in one move, so it is either complete or not there. The include and library paths (`UnibindIncludeDir`, `UnibindLibDir`, `V8Dir`, `SpiderMonkeyDir`) are derived from the version, so a version bump repoints every path and re-fetches rather than silently reusing an old library. `EngineFlavor` follows `$(Configuration)` (a debug unibind links the debug engines); `EngineToolset` is the MSVC toolset the archives were built with - a floor, not a match. The backend (`lod114d`) and `js_tests` fetch nothing. `build.ps1 deps` runs just these targets (with `-Platform x64`, only unibind's).

## Game Abstraction Layer

The game layer decouples the JS API from direct game memory access, enabling multi-version support.

### Design Principles

1. **No engine dependency**: The game interface uses only standard C++ types. No unibind or engine headers, no API layer headers.
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

The `reference/d2bs/` directory contains the original d2bs implementation (SpiderMonkey-based) used for cross-referencing when implementing the JS API. Key files:

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

### Constants: look for the existing one before declaring a new one

Most game magic numbers are already named somewhere. Search before you type a
literal, in this order:

1. **The header that owns the value.** In a backend, D2MOO is the source of truth:
   `D2Constants.h` (`D2C_UIvars` - `UI_CUBE`, `UI_NPCSHOP`, ...), `D2StatList.h`
   (`D2C_ItemStats` - `STAT_HITPOINTS`, `STAT_STATPTS`, ...), `Units/Units.h`
   (`D2C_UnitTypes`), `D2Monsters.h` (`D2C_MonTypeFlags`). Most are already in scope
   through `imports/D2Common.h`. Platform values likewise: `WM_LBUTTONDOWN`, not `0x201`.
2. **`src/contract/game/`** - `Constants.h` and the enums in `Types.h` (`UnitType`,
   `MonsterSpecType`, `ItemLocation`, ...). The frontend and the contract cannot see
   D2MOO, so these mirror the handful of values the JS API needs; use them there
   instead of re-typing the literal.
3. **The backend's shared headers** - e.g. `SUBTILE_SCALE` in
   `backends/lod114d/game/DrlgHelpers.h`.

When a value genuinely has to be restated, derive it from the canonical definition
rather than repeating the literal:

```cpp
constexpr uint32_t STAT_FIXED_POINT_FIRST = STAT_HITPOINTS;
```

**Scope it to its use.** A constant used by one file stays in that file's anonymous
namespace - no shared header, no namespace qualification at the call site. Promote it
only when a second file needs the same value, and then delete every copy.

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
- Exceptions: third-party signatures (Win32, sqlite, ImGui) may require `int`

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

### Script Values (`api/core/Convert.h`, `Extract.h`, `Error.h`)

The frontend speaks unibind (`ub::`) only - never an engine type. Use the helpers in `api/core/` rather than raw `ub::` factories:

- **Native -> script**: `convert::ToJS(isolate, value)` for `const char*`, `std::string`, `std::string_view`, `std::filesystem::path`, `int32_t`, `uint32_t`, `double`, `bool` (strings are lossy UTF-8: invalid bytes become U+FFFD; an empty handle means the engine is out of memory). `convert::ToJS(context, value)` for `Point` / `Position` / `Size` / `StatEntry` / `StatListEntry` returns `std::optional<ub::Local<ub::Object>>` - `{x, y}`, `{width, height}`, ... - empty if a step failed with the engine's exception pending. For a plain number or bool return, `GetReturnValue().Set(nativeValue)` needs no conversion.
- **Script -> native**: `convert::ToString` / `ToInt32` / `ToUint32` / `ToDouble` / `ToBool(context, value)`, with the reference's leniency (absent / null / undefined, or a throwing conversion, is the zero value). `extract::Point` / `Position` / `Size` read an `{x, y}` / `{width, height}` object or two adjacent arguments (`extract::Point(args, 0)`), returning `std::optional`; `extract::PointInto` / `SizeInto` write straight into a drawable's atomic.
- **Throwing and argument checks**: `error::ThrowError` / `ThrowTypeError` / `ThrowRangeError(isolate, message)`, `error::CheckArgCount` / `CheckIsNumber` / `CheckIsString(args, ...)`, `error::WarnAndReturnFalse(args, message)` (log + return `false`, the reference's soft failure). A throw takes effect when the callback returns to the engine, so return right after one.

Every `ub::` operation that can fail returns a `std::optional`; check it (`value_or(false)`, `if (auto x = ...)`) rather than assuming success.

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
auto p = extract::Point(args, 0).value_or(game::Point::Zero);
return game::Position::Zero;

// Avoid
auto p = extract::Point(args, 0).value_or(game::Point{});
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

### Class Bindings (`api/core/Class.h`)

Every script-visible class derives from `api::ClassBase<Derived, NativeType>`, a CRTP base over `ub::Class<NativeType>`. Derived supplies a `ClassName` and a `Configure` that declares its members with `+[]` lambdas; the members' `///` comments are what `scripts/extract_api.py` reads:

```cpp
class JSExit : public ClassBase<JSExit, navigation::ExitInfo> {
   public:
    static constexpr std::string_view ClassName = "Exit";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description The exit's X coordinate in world coordinates.
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSExit>(info);  // TypeError "Illegal invocation" + null on a foreign receiver
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->pos.x);
            });
    }
};
```

- **Members**: `Property(cls, name, getter[, setter])` (read-only with one lambda, read-write with two) as an own property of every instance, `Method(cls, name, fn)` on the prototype, `StaticMethod(cls, name, fn)` on the constructor, `SymbolMethod(cls, ub::WellKnownSymbol::..., name, fn)` for `Symbol.iterator`. Signatures: getter `(const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo&)`, setter `(const ub::Local<ub::Name>&, const ub::Local<ub::Value>&, const ub::PropertyCallbackInfo&)`, method `(const ub::CallbackInfo&)`. All of them are routed through the `components/script/NativeCallHook` trampolines (per-script stack capture, the Profiling panel's per-binding stats, named `"Class.member"`), so never bypass them with a raw `cls.Accessor` / `cls.Method`.
- **Own properties**: `Property` declares its accessor on the class's instance template (`cls.InstanceTemplate()`), not the prototype, so it is an own property of each instance. Scripts rely on that: kolbot copies game objects with `for...in` + `hasOwnProperty` (`copyObj`), and `Object.keys` / `JSON.stringify` see only own properties. Keep it there.
- **The receiver**: read `info.This()` / `args.This()`, never a holder. `Receiver<JSFoo>(info)` (`api/classes/game/Receiver.h`) unwraps it and throws on a foreign receiver; `Unwrap(value)` returns null silently, `UnwrapShared(value)` a share of the native, `IsInstance(value)` a bool.
- **Construction**: without a `New` the class is not constructable from script (and appears so in the docs). With one, `static std::unique_ptr<Native> New(const ub::CallbackInfo& args)` or `static std::shared_ptr<Native> New(...)` builds the native, returning null after throwing to refuse. Calling without `new` is a TypeError unless the class also declares `static constexpr bool CALLABLE_WITHOUT_NEW = true;`, in which case `New` tells the two apart with `args.IsConstructCall()` - `JSProfile` does this, gated on the `profileCallWithoutNew` compatibility flag:

  ```cpp
  static constexpr bool CALLABLE_WITHOUT_NEW = true;
  static std::unique_ptr<ProfileData> New(const ub::CallbackInfo& args);
  // in New:
  if (!args.IsConstructCall() && !config::CompatibilityFlags::Instance().IsEnabled("profileCallWithoutNew")) {
      error::ThrowTypeError(isolate, "Profile must be called with 'new'");
      return nullptr;
  }
  ```
- **Handing out instances**: `JSFoo::Wrap(context, std::make_shared<Native>(...))` returns `std::optional<ub::Local<ub::Object>>` without running `New`:

  ```cpp
  if (auto result = Wrap(context, std::make_shared<game::Unit>(*invItem))) {
      args.GetReturnValue().Set(*result);
  }
  ```
- **Registration**: a new class is added to `api/classes/ClassRegistry.cpp` twice - `Install<JSFoo>(context, "Foo")` in `RegisterAllClasses` (puts the constructor on the global object) and `JSFoo::ClearCache()` in `ClearAllClassCaches` (the declared class is cached per thread and isolate, and must be forgotten when the isolate goes). Members that exist only on `me` are declared in `CreateMeObject` with `JSUnit::InstanceProperty(context, me, name, getter[, setter])`, which uses the same trampolines.
- **Instance counting** is automatic: every wrapper's share is counted in `InstanceTracker` (the console's Scripts panel) through the share's deleter.

### Class Ownership Model

`ub::Class<T>` holds each instance's native as a `std::shared_ptr<T>`; the native goes with its last share, whether that is the wrapper (collected by the GC) or something else that kept one.

| Category | Classes | Native | Constructor |
|----------|---------|--------|-------------|
| Game Data | Unit, Room, Area, Exit, PresetUnit, Party, Control, StashTab | Identity handle / value copy (`game::Unit`, `navigation::ExitInfo`, ...); resolved per call | none - handed out by `getUnit()` etc. via `Wrap` |
| Script-Owned I/O | File, Folder, Socket, DBStatement | `*Data` struct owning the OS / library resource | none - made by a static factory (`File.open`, `Socket.open`), a global (`dopen`) or another object (`SQLite.query`; a statement keeps its database alive) |
| Script-Owned, constructable | SQLite, Profile, Sandbox | `*Data` struct | `New` returning `std::unique_ptr` (Profile is `CALLABLE_WITHOUT_NEW`) |
| Screen Hooks | Frame, Box, Line, Text, Image | `std::shared_ptr<*Drawable>` also held by the owning `Script` (`AddDrawable`) and read by the game thread | `New` returning `std::shared_ptr` |
| Script handle | D2BSScript | `ScriptHandle` naming a running script | none - `getScript()` etc. |
| Namespaces | FileTools, HttpClient, Compatibility, TxtTables | never instantiated | none - static methods only |

### Game Thread Safety

Game types are identity-based handles with per-frame pointer caching (`HandleCache`). See `docs/game_thread_safety.md` for full details.

- **GameReadLock** (in `ResolvePtr()`) - automatic, per-resolve. Scripts never block each other.
- **Bridge::Lock()** - explicit, for binding callbacks that iterate game data or do multi-step traversals. Take it before the traversal, after the cheap argument checks.
- **GameWriteLock** - game thread only. Held continuously across the frame body; released during `GameLoop::OnSleep`'s drain loop in `idleSleepInterval` slices so script readers can acquire `GameReadLock`, then reacquired before returning to the game's frame work. Bootstrap via `firstSleep_` first-tick handling.
- **GameThread::Execute()** - post work to game thread from scripts. For menu operations requiring game thread (login, createGame, etc.).

When implementing stubs: simple property reads just work (ResolvePtr handles locking). Iterating game linked lists or mutating game state needs `Bridge::Lock()`. Menu UI operations need `GameThread::Execute()`.

### Game Abstraction Patterns

**JS API -> Game Layer delegation:**
```cpp
// Property getter: the native IS the game handle; it resolves (under a GameReadLock) per call
auto* data = Receiver<JSUnit>(info);
if (data == nullptr || !*data) {
    return;
}
info.GetReturnValue().Set(data->Pos().x);

// Method that needs the game: WaitForGameReady, soft-fail like the reference
if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
    error::WarnAndReturnFalse(args, "Game not ready");
    return;
}

// Multi-step traversal: hold one read lock across it, then wrap the result
auto lock = game::Bridge::Lock();
auto invItem = data->FindFirstInventoryItem(cursor);
if (invItem) {
    if (auto result = Wrap(context, std::make_shared<game::Unit>(*invItem))) {
        args.GetReturnValue().Set(*result);
    }
}

// Control methods require menu state
if (game::GetGameState() != game::GameState::Menu) {
    return;
}
```

**The `me` object** is a `Unit` wrapper around `game::Unit::Player()` - the unitId=0, type=0 sentinel whose `ResolvePtr()` returns the current player unit - plus `me`-only members declared with `JSUnit::InstanceProperty` in `ClassRegistry.cpp`'s `CreateMeObject`.

**Where search/filter logic lives:**

The game layer owns only **identity lookups** (`Find(id)`, `Level::Get(no)`, etc.), **iteration primitives** (`GetFirstInGame(type)` / `GetNextInGame()`, `GetFirstItem()` / `GetNextItem()`, `GetFirst()` / `GetNext()` on Room/Party/Control, etc.), **per-handle game-memory reads** (`Pos()`, `Name()`, etc.), and the small number of game-specific dispatchers that can't be expressed as primitive composition (e.g. `Unit::GetOwner()` which branches on `Type()` between `GetMonsterOwner` and the dwOwnerId field). Any operation that composes primitives + match logic (filtered searches, cross-entity walks, bulk collects) lives in `src/contract/game/Finders.h` as inline method definitions on the relevant handle class. Port authors reimplement primitives, accessors, and dispatchers only; filter logic is shared across all game versions.

**Import type alignment (cast elimination):**

When you see narrowing/widening `static_cast` at call sites of `imports::*` functions, suspect the import declaration has the wrong type - usually `uint32_t` where the game actually uses `int32_t` / `uint16_t` / a real struct pointer. Source of truth for 1.14d signatures is `reference/d2bs/D2Ptrs.h` (FUNCPTR / VARPTR macros); D2MOO's typed structs in `dependencies/D2MOO/source/` are the source for pointer types. Fix the import declaration first, then drop the casts. Width-change casts (`uint32_t` <-> `uint16_t`) need verification that no caller relies on the upper bits; same-bit-pattern signedness flips are safer.

Prefer narrow fixes (just the import + d2bs callers) over wide ones (touching the framework `game/*.h` contract). The framework game contract is consumed by every port and by JS bindings - changing it has wide blast radius. Only widen the fix when the JS API surface already wants the more-specific type AND no port works around the current one.

**Bridge initialization contract:**

`game::Bridge::Init()` returns `bool`. The d2bs port's implementation pops a `MessageBoxW(MB_OK | MB_ICONERROR, "d2bsng init failure", ...)` at each distinct failure point (module-base resolution, import registry mismatch, asm-thunk resolve, hook install, per-import probes) and returns `false`. `DllMain`'s `DLL_PROCESS_ATTACH` returns `FALSE` on Init failure to abort DLL load. New version ports MUST keep this shape - silent failures lead to "the bot stopped working" with no diagnostic.

**Port-chosen message sink:**

`d2bs::game::console::OnMessage` is the port-chosen sink. The backend routes to the frontend console window through the `onConsoleMessage` callback in the `GameCallbacks` table (the frontend registers it to `d2bs::runtime::console::OnMessage`); the symmetric `onConsoleDrawFrame` callback lets the port console host render the frontend panels. Routing through callbacks keeps the backend free of any frontend dependency - the port still chooses the sink, it just resolves it via `hooks::GetActiveCallbacks()` instead of including the frontend directly. See `src/backends/lod114d/game/Console.cpp`.

**JS API stability:**

The JS-visible surface lives in `src/frontends/runtime/api/` (`globals/*.cpp` for free functions, `classes/**` for script-visible classes). When refactoring C++ helpers consumed by these adapters (e.g. changing `GetTradeInfo` from a struct return to `std::optional<std::string>`), audit the adapter for any output-shape change. Internal C++ evolution (struct -> optional, int -> bool, helper inlining) is fine; observable JS behavior changes are not, unless explicitly approved. When in doubt, `git diff -- src/frontends/runtime/api/` after a refactor and read every JS-visible Set/SetNull/SetReturnValue path.

### Native Data Structs

A class that owns a resource has a `*Data` struct as its native (game classes use the game handle or value type directly). The destructor releases the resource, because the native goes whenever its last share does - on whichever thread that is:

```cpp
struct SQLiteData {
    sqlite3* handle = nullptr;
    std::filesystem::path path;
    bool isOpen = false;
    // Statements prepared on this connection. Weak: a statement keeps its database alive, not the other way round.
    std::vector<std::weak_ptr<DBStatementData>> statements;

    void Close() noexcept;  // Idempotent cleanup - closes all statements and the database
    ~SQLiteData() noexcept { Close(); }
};
```

### Global Functions

Organized by category in `src/frontends/runtime/api/globals/`, one `Register*Functions(const ub::Context&)` per file (called from `Script::SetupIsolate`). Each function is a `function::Register` call (`api/core/Function.h`) with its `///` doc block above it; `Register` routes it through the same NativeCallHook trampoline as class members:

```cpp
/// @description Pauses the calling script for the given milliseconds while still processing its events.
/// @signature delay(ms: number)
/// @param ms {number} - milliseconds to wait; clamped to a minimum of 1
/// @returns {undefined}
function::Register(
    context, "delay", +[](const ub::CallbackInfo& args) {
        if (!error::CheckArgCount(args, 1, "delay")) {
            return;
        }
        uint32_t ms = std::max(convert::ToUint32(args.GetContext(), args[0]), 1U);
        // ...
    });
```

- `CoreFunctions.cpp` - print, delay, getTickCount, include, load, sendPacket, sendClick, setSpeed/getSpeed, etc.
- `GameFunctions.cpp` - getUnit, getPath, getRoom, clickMap, acceptTrade, etc.
- `MenuFunctions.cpp` - login, createGame, joinGame, getLocation, timers, events
- `HashFunctions.cpp` - md5, sha1, sha256, sha384, sha512 (with file variants)
- `Constants.cpp/h` - JS-visible constants (FILE_READ, FILE_WRITE, FILE_APPEND, ProfileType), declared read-only with `Define(context, target, name, value)` in `RegisterConstants`
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
ub::Local<ub::Value> argv[] = {arg1, arg2};
// GOOD:
const std::array<ub::Local<ub::Value>, 2> argv = {arg1, arg2};
auto result = fn.Call(context, ub::Undefined(isolate), argv);
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
