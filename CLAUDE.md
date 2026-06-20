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
#   test           - Build and run the test suite (framework_tests.exe)
```

The build script auto-detects the Visual Studio installation via vswhere and works from any directory. You can also build directly with MSBuild or in Visual Studio.

## Tests

The test project (`tests/framework/framework_tests.vcxproj`) is a standalone console exe using [doctest](https://github.com/doctest/doctest). It compiles the real `Pathfinder.cpp` with fake game layer implementations (no DLL, no V8, no game). Tests do not link any static libs - they compile sources directly alongside test fakes.

```bash
# Build and run all tests:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File build.ps1 test

# Run specific tests by name filter:
Release/framework_tests.exe -tc="Walk A*"

# List all test cases:
Release/framework_tests.exe -ltc

# Verbose output (show all assertions + benchmark messages):
Release/framework_tests.exe -s

# Run only benchmarks:
Release/framework_tests.exe -tc="Benchmark*,Kurast*,Real*" -s
```

### What the tests cover

Currently tests only cover the **pathfinding engine** (`src/framework/components/pathfinding/`):

- **Unit tests** (`tests/framework/pathfinding/tests/`): CollisionLookup queries, walk A*, teleport A*, walk reduction, point mutation, edge cases, penalty avoidance
- **Reference comparison** (`tests/framework/pathfinding/tests/comparisons/`): exact-match and cost-equivalence tests comparing our A* output against an adapted reference A* implementation on identical collision grids
- **Benchmarks**: synthetic 2000x2000 grids and real game collision data (`.d2col` fixtures in `tests/framework/fixtures/maps/`) comparing walk and teleport modes against the reference
- **Fixture system** (`tests/framework/fixtures/`): binary `.d2col` loader for collision grids dumped from the game via `tools/dump_collision.js`

### Adding real game data for benchmarks

1. Load `tools/dump_collision.js` in d2bs while in-game - dumps `.d2col` files per level
2. Copy `.d2col` files to `tests/framework/fixtures/maps/`
3. Run `.\build.ps1 test` - real-world benchmarks auto-discover and use them

**Requirements**: Visual Studio 2022 with ClangCL toolchain, Windows SDK 10.0.26100.0

## API Documentation

The script-visible JS API is documented by a generator pipeline under `scripts/`. The bindings in `src/framework/api/` carry structured `///` doc comments (`@description`, `@signature`, `@param`, `@returns`, `@throws`, `@callback`, `@event`, `@type`, `@mode`); their exact vocabulary lives at the top of `extract_api.py`. The `{type}` fields may be object literals / unions / generics (`{x:number,y:number}`, `Unit|null`, `Array<{x:number}>`) - the parser is brace-balanced.

| Script | Flow | Notes |
|--------|------|-------|
| `extract_api.py` | `src/framework/api/**` -> `api.json` | Walks the V8 bindings with **libclang**, emitting the full surface (classes, globals, constants, the `me` object, events) with a per-entry `since` from git tags. Needs the same vcpkg + V8 + MSVC/SDK include set the build uses. The only script that needs libclang. |
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

The codebase is split into three build targets (two static libs + one DLL) under `src/`, plus a test project:

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
│   ├── framework/          framework.lib - scripting engine + JS API
│   │   ├── api/            V8 bindings: classes/ (game, io, scripting, drawing), globals/, core/
│   │   ├── components/     Framework internals:
│   │   │   ├── config/         AppConfig, IniConfigStore, Version
│   │   │   ├── script/         Script engine (V8 isolate mgmt) + compat shims
│   │   │   ├── v8/             V8 host initialization
│   │   │   ├── events/         Event system
│   │   │   ├── gameloop/       Per-frame game-thread loop + lock release
│   │   │   ├── pathfinding/    A* pathfinder
│   │   │   ├── console/        ImGui dev console (log/REPL/scripts/stacktraces/threads/settings)
│   │   │   ├── inspector/      V8 inspector (Chrome DevTools) debug server
│   │   │   ├── drawing/        Screen-hook drawables (Box/Frame/Line/Text/Image)
│   │   │   ├── characterstate/ Character-state snapshot -> D2BotNG manager (WM_COPYDATA)
│   │   │   ├── speedhack/      Global game-time scaling
│   │   │   ├── proxy/          SOCKS5 bypass scope for script sockets
│   │   │   ├── dde/            DDE service
│   │   │   ├── exits/          Level-exit finder
│   │   │   ├── profile/        Profile DTOs
│   │   │   ├── update/         GitHub-release update checker (6h poll -> in-game notice)
│   │   │   └── Framework.h/.cpp DLL lifecycle orchestrator
│   │   └── game/           Game interface headers (NO .cpp files)
│   └── lod114d/            d2bs.dll - 1.14d game implementation
│       ├── dllmain.cpp     DLL entry point
│       ├── game/           1.14d implementation (.cpp + internal .h)
│       ├── imports/        Typed game func/var registry + 1.14d offsets + extras/ structs
│       ├── hooks/          Inline / IAT hooks (incl. WS2_32 connect SOCKS5 hook)
│       ├── asm_thunks/     Hand-written ABI thunks
│       └── console/        Port console host (window + GL + ImGui glue)
└── tests/
    └── framework/          framework_tests.exe - doctest suite (pathfinding) + fakes
```

### Build Targets

| Target | Type | Output | Contents |
|--------|------|--------|----------|
| **utils** | Static lib | `Release/utils.lib` | `src/utils/` - crypto, threading, stackwalker |
| **framework** | Static lib | `Release/framework.lib` | `src/framework/` - api/, components/, game/ interface headers. Has unresolved game:: symbols. |
| **d2bs** | DLL | `Release/d2bs.dll` | `src/lod114d/` - 1.14d game implementation + dllmain. Links framework.lib + utils.lib, resolves all symbols. |
| **framework_tests** | Console EXE | `Release/framework_tests.exe` | `tests/framework/` - doctest tests with fake game layer |

### How Linking Works

```
utils.lib       <- fully resolved, standalone
     v
framework.lib   <- has UNRESOLVED symbols: game::Unit::Pos(), game::Bridge::Init(), etc.
     v            (declared in src/framework/game/*.h, no .cpp here)
     v
d2bs.dll        <- src/lod114d/game/*.cpp resolves all game:: symbols
                  Links: framework.lib + utils.lib + v8_monolith.lib
                  LTO inlines thin wrappers across all three
```

### Game Interface vs Implementation

The game abstraction is split across two directories:

- **`src/framework/game/`** - Interface headers only (17 `.h` files). Defines the wrapper classes (`Unit`, `Room`, `Level`, etc.) with method declarations using opaque `void*` pointers. Part of `framework.lib`. No game-version-specific code.

- **`src/lod114d/game/`** - 1.14d implementation (12 `.cpp` files + internal headers like `RoomData.h` / `DrlgHelpers.h`). Part of `d2bs.dll`. The version-specific game-function/variable bindings, structs, and hooks live alongside it under `src/lod114d/imports/` (typed import registry + per-DLL declarations), `src/lod114d/imports/extras/` (structs not in D2MOO), `src/lod114d/asm_thunks/`, and `src/lod114d/hooks/`.

To add support for a different game version: create a new sibling directory under `src/` with its own `game/` implementation and vcxproj, linking the same `framework.lib` and `utils.lib`.

### Include Path Strategy

Each project has specific include directories that make cross-project includes work:

| Project | Include Directories |
|---------|-------------------|
| **utils** | `$(ProjectDir)` |
| **framework** | `$(ProjectDir)` ; `$(SolutionDir)src` ; V8 include |
| **d2bs DLL** | `$(ProjectDir)` ; `$(SolutionDir)src\framework` ; `$(SolutionDir)src` ; `$(ProjectDir)game` ; D2MOO include roots ; V8 include |
| **framework_tests** | `$(SolutionDir)src\framework` ; `$(SolutionDir)src` ; `$(ProjectDir)` |

All includes use project-root-relative paths - never use relative paths like `../`:
```cpp
#include "game/Unit.h"                    // Interface header (from framework)
#include "api/core/V8Class.h"             // Framework internal
#include "components/config/AppConfig.h"  // Config (under components)
#include "utils/utils.h"                  // Cross-project utils include
#include "RoomData.h"                     // Same-dir include (in lod114d/game/)
```

Same-directory includes use quotes without path: `#include "ClassRegistry.h"`

### Dependency Rules

These are the intended dependencies. A few deliberate exceptions are noted inline - each one lets a layer reuse an existing type or helper instead of duplicating it, which is the cheaper trade-off than the indirection that removing the edge would require.

- **utils/** depends on: standard library, Windows headers, third-party libs (spdlog, stackwalker).
- **framework/game/** (interface) depends on: standard library only. NEVER on V8 or api/. NEVER on components/, with one exception: `game/Menu.h` includes `components/profile/ProfileData.h` so `Login()` can take the profile struct by const-ref rather than duplicating that DTO into the game layer.
- **framework/api/** depends on: game/ interface, components/, utils/, V8.
- **framework/components/** depends on: game/ interface, components/config/, utils/, V8. Exception: `components/script/` includes `api/` - the script engine is the JS-API composition root (it owns V8 isolate setup and registers the `api/` ClassRegistry + globals), and a few components reuse `api::v8_convert` instead of duplicating the V8 conversion helpers. `components/update/` similarly reuses the V8-free `api::classes::PerformHttpRequest` (`api/classes/io/HttpEngine.h`) rather than re-implementing the WinHTTP plumbing.
- **lod114d/game/** (implementation) depends on: game/ interface, utils/, and sibling port headers (imports/, hooks/, asm_thunks/). NEVER on api/ or V8. NEVER on components/, except config reads (`components/config/AppConfig.h`) and forwarding to the port-chosen console sink (`components/console/`, see "Port-chosen message sink" below).

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

The test project (`tests/framework/`) has its own `vcpkg.json` with just `doctest` (header-only test framework).

## Game Abstraction Layer

The game layer decouples the JS API from direct game memory access, enabling multi-version support.

### Design Principles

1. **No V8 dependency**: The game interface uses only standard C++ types. No V8 headers, no API layer headers.
2. **Thin wrappers**: Each wrapper class is `sizeof(void*)` - one pointer, no vtable. Zero overhead with LTO.
3. **Gaps marked with TODO**: Methods read live game state; the few unimplemented spots are marked `TODO(implement)` with comments describing what is needed.
4. **Opaque pointers**: Wrappers hold a single `void*`. The framework interface stays game-version-agnostic; typed D2MOO structs are used only inside the implementation layer (`src/lod114d/`), never at the framework boundary.

### File Organization

`src/framework/game/` hosts **two kinds** of headers:

1. **Abstraction interfaces** - declarations implemented per-game under `src/lod114d/game/` (or future ports like 1.13c, D2R). Port authors must provide `.cpp` files for these: `Unit.h`, `Room.h`, `Level.h`, `Party.h`, `Control.h`, `Sprite.h`, `Menu.h`, `Console.h`, `Bridge.h`, `GameCallbacks.h`, and the game-specific declarations in `GameHelpers.h`.

2. **Framework-owned utilities** - fully implemented in framework, operate on the interfaces above, have no game-specific counterpart. Port authors implement nothing here; they get these for free: `Finders.h`, `Types.h`, `HandleCache.h`, `GameLock.h`, `GameThread.h`, `Constants.h`.

Within each handle header (`Unit.h`, `Room.h`, etc.), a comment separator marks the bucket-1 (game-impl required) and bucket-2 (framework-impl, inline in `Finders.h`) sections of the class.

**Interface** (`src/framework/game/`):

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

**Implementation** (`src/lod114d/game/` - 1.14d specific):

| File | Purpose |
|------|---------|
| `game/*.cpp` | Implementations of all interface methods |
| `imports/ImportTypes.h` | Typed import registry: `GameFunc<Cc,Sig>` / `GameVar<T>`, resolved against the module base in `Bridge::Init()` |
| `imports/*.h` (D2Client, D2Common, D2Game, D2Win, ...) | Per-DLL game function / variable declarations with their 1.14d offsets |
| `imports/extras/*.h` | Game structs NOT in D2MOO, with `static_assert` size checks |
| `imports/D2MOOConfig.h` | Selects the 1.14d D2MOO build |
| `asm_thunks/*`, `hooks/*` | Hand-written ABI thunks and inline / IAT hooks |

### Framework vs Game Separation

| Belongs in lod114d/game/ (impl) | Belongs in components/config/ | Belongs in framework |
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
| `TODO(compatibility)` | `src/framework/components/script/CompileSource.{h,cpp}`, `src/framework/api/classes/scripting/JSProfile.cpp` | SpiderMonkey/kolbot-era compat shims; later gate behind a per-script compatibility flag |

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
// Port contract moved to a dedicated header (src/framework/game/Console.h)
// and implementation file (src/lod114d/game/Console.cpp). Not redeclared here.
```

**Bad:**
```cpp
// Replaces the previous low-level game::SendKeyPress(msg, key, extra).
void SendKey(uint32_t key);
```

**Good:** no comment at all (or a comment that describes what the current thing *is*, not what it *used to be*).

The same applies to the doc strings: don't mention that `Message` "was previously called X" or that `SplitByColor` "used to live in framework/components/". Current behavior only.

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

Use `std::array` instead of C-style arrays. This applies everywhere including game structs in `src/lod114d/imports/extras/` - `std::array` has identical memory layout to C arrays (`sizeof(std::array<T,N>) == sizeof(T[N])`) so binary compatibility is preserved. All struct sizes are verified by `static_assert`.

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

The shared 2D types live in `src/framework/game/Types.h`:
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

The game layer owns only **identity lookups** (`Find(id)`, `Level::Get(no)`, etc.), **iteration primitives** (`GetFirstInGame(type)` / `GetNextInGame()`, `GetFirstItem()` / `GetNextItem()`, `GetFirst()` / `GetNext()` on Room/Party/Control, etc.), **per-handle game-memory reads** (`Pos()`, `Name()`, etc.), and the small number of game-specific dispatchers that can't be expressed as primitive composition (e.g. `Unit::GetOwner()` which branches on `Type()` between `GetMonsterOwner` and the dwOwnerId field). Any operation that composes primitives + match logic (filtered searches, cross-entity walks, bulk collects) lives in `src/framework/game/Finders.h` as inline method definitions on the relevant handle class. Port authors reimplement primitives, accessors, and dispatchers only; filter logic is shared across all game versions.

**Import type alignment (cast elimination):**

When you see narrowing/widening `static_cast` at call sites of `imports::*` functions, suspect the import declaration has the wrong type - usually `uint32_t` where the game actually uses `int32_t` / `uint16_t` / a real struct pointer. Source of truth for 1.14d signatures is `reference/d2bs/D2Ptrs.h` (FUNCPTR / VARPTR macros); D2MOO's typed structs in `dependencies/D2MOO/source/` are the source for pointer types. Fix the import declaration first, then drop the casts. Width-change casts (`uint32_t` <-> `uint16_t`) need verification that no caller relies on the upper bits; same-bit-pattern signedness flips are safer.

Prefer narrow fixes (just the import + d2bs callers) over wide ones (touching the framework `game/*.h` contract). The framework game contract is consumed by every port and by JS bindings - changing it has wide blast radius. Only widen the fix when the JS API surface already wants the more-specific type AND no port works around the current one.

**Bridge initialization contract:**

`game::Bridge::Init()` returns `bool`. The d2bs port's implementation pops a `MessageBoxW(MB_OK | MB_ICONERROR, "d2bsng init failure", ...)` at each distinct failure point (module-base resolution, import registry mismatch, asm-thunk resolve, hook install, per-import probes) and returns `false`. `DllMain`'s `DLL_PROCESS_ATTACH` returns `FALSE` on Init failure to abort DLL load. New version ports MUST keep this shape - silent failures lead to "the bot stopped working" with no diagnostic.

**Port-chosen message sink:**

`d2bs::game::console::OnMessage` is the port-chosen sink. The d2bs port routes to the framework console window (`d2bs::framework::console::OnMessage`). The framework->game->framework call shape isn't a layering bug - it's the abstraction working as designed. See `src/lod114d/game/Console.cpp`.

**JS API stability:**

The JS-visible surface lives in `src/framework/api/` (`globals/*.cpp` for free functions, `classes/*.cpp` for V8-exposed classes). When refactoring C++ helpers consumed by these adapters (e.g. changing `GetTradeInfo` from a struct return to `std::optional<std::string>`), audit the adapter for any output-shape change. Internal C++ evolution (struct -> optional, int -> bool, helper inlining) is fine; observable JS behavior changes are not, unless explicitly approved. When in doubt, `git diff -- src/framework/api/` after a refactor and read every JS-visible Set/SetNull/SetReturnValue path.

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

Organized by category in `src/framework/api/globals/`:
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
