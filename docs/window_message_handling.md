# Window message and input handling

How d2bs intercepts and injects Windows messages. The game-window half is
backend-independent and lives in `src/core/input/InputHook.{h,cpp}` (namespace
`d2bs::input`); a backend's `hooks/HookManager.cpp` installs it and supplies the
callbacks it dispatches to. The console-window half is 1.14d-specific, because a
backend that renders its console as an in-game overlay needs no second window.
Two OS windows are involved:

- **Game window** - D2's main window, owned by the game (render) thread.
- **Console window** - the d2bs dev console, owned by the console render thread
  (spawned by `console::Init`).

The console window lives in `src/backends/lod114d/console/Console.cpp`.

## Game-window input: the WH_GETMESSAGE hook

The core of human-input handling is a single `WH_GETMESSAGE` hook
(`GetMsgProc`), not the low-level `WH_KEYBOARD` / `WH_MOUSE` hooks. It is
installed on the game thread (see Threading below) and fires inside the game's
`GetMessage` / `PeekMessage` as a message is about to be returned.

Why `WH_GETMESSAGE` instead of `WH_KEYBOARD` / `WH_MOUSE`:

- It can **rewrite** the `MSG`, not just request a block. We never discard a
  message: D2's pump does `PeekMessage(PM_NOREMOVE)` then a blocking
  `GetMessage`, so a swallowed message makes the blocking get wait forever and
  freezes the frame loop while, e.g., the cursor hovers a hostile. To block an
  input we instead rewrite it to `WM_NULL` (`Neutralize`) - `GetMessage` still
  returns, the pump keeps turning, and the game ignores the no-op.
- It acts only on `wParam == PM_REMOVE`. `PeekMessage(PM_NOREMOVE)` probes must
  still see the real message so D2 takes its drain-the-queue branch; the message
  is neutralized later when it is actually removed.

Dispatch path (all in `InputHook.cpp`):

- `GetMsgProc` routes `WM_MOUSEFIRST..WM_MOUSELAST` to `ProcessMouse` and
  `WM_KEYFIRST..WM_KEYLAST` to `ProcessKey`.
- `ProcessMouse` / `ProcessKey` first honor the injected-input tag (below), then
  offer the message to the backend's `intercept` hook (1.14d sets none; a backend
  that draws its console in-game routes to it there), then call
  `HandleMouseMessage` / `HandleKeyMessage`, which derive the `ClickButton` /
  `Position` / `KeyState` from the `MSG` and dispatch to the `onMouseClick` /
  `onMouseMove` / `onKeyEvent` hooks, returning whether to block. A block (from
  `blockMouse` / `blockKeys` read off `AppConfig`, a consuming intercept, or a
  callback that consumes the event) results in `Neutralize`.
- Character messages (`WM_CHAR` and friends) pass through unless a backend's
  `intercept` consumes them, which is offered first; when `blockKeys` is set,
  the originating key-down was already neutralized before `TranslateMessage`
  ran, so any character left in the stream is our own.

### Backend wiring

`input::Install(hwnd, hooks)` takes an `input::Hooks` table of plain function
pointers mirroring the input entries of `GameCallbacks` - `onMouseClick`,
`onMouseMove`, `onKeyEvent`, `onIPC` - plus the optional `intercept`. Each
backend's `HookManager.cpp` builds it from the active `GameCallbacks` table
(`BuildInputHooks`), so core never sees a backend or the callbacks table itself.
A null entry leaves the hook empty, which the dispatcher treats as "not
handled".

They are function pointers rather than `std::function` because `Remove()`
clears the table from a different thread than the one dispatching: tearing down
a `std::function` under a caller destroys the callable while it is executing.
Each dispatch site loads its pointer once into a local for the same reason, so
a clear between the null check and the call cannot turn the guard into a null
call.

## Human vs injected input (tagging)

The same hook must let the bot's own synthetic clicks / keystrokes through even
while `blockKeys` / `blockMouse` is suppressing the human at the keyboard.
Injected messages are tagged in bits that real hardware leaves clear, and the
tag is stripped before the game sees the message:

- Mouse: `INJECTED_MOUSE_TAG` in `HIWORD(wParam)` (unused for the button
  messages we post).
- Key: `INJECTED_KEY_TAG` in `lParam` bit 25 (a reserved keystroke-flag bit,
  zero on real input).

`input::PostInjectedInput(hwnd, message, wParam, lParam)` (`InputHook.h`)
applies the right tag and `PostMessageW`s the message. `GetMsgProc` sees the
tag, clears it, and lets the message reach the game unblocked and without
re-dispatching it as a human event.

Callers:

- `game::SendClick` / `game::SendKey` (1.14d `game/GameHelpers.cpp`), exposed to
  scripts via the `sendClick` / `sendKey` globals (`api/globals/CoreFunctions.cpp`).
- Control clicks (1.14d `game/Control.cpp`).

## Game-window WndProc subclasses

Two subclasses sit on the game HWND. Each saves the previous `WNDPROC` and
chains to it with `CallWindowProcW`, so order does not matter:

1. `SubclassedWndProc` (`InputHook.cpp`) - handles `WM_COPYDATA` and forwards
   the payload to the `onIPC` hook, which the backend wires to
   `GameCallbacks::onIPC` (the launcher / inter-instance IPC channel; matches
   the reference launcher's `CF_TEXT`-style null-terminated convention, so the
   trailing NUL is stripped). Everything else chains through.
2. `TitleSubclassProc` (`Console.cpp`) - observes `WM_SETTEXT` so the console
   window can mirror the game window's title (multi-instance titling). It does
   not change behavior; it `PostMessage`s `WM_GAME_TITLE_CHANGED` to the console
   and lets the original proc do the real title update. Installed lazily from the
   console render loop (`EnsureGameTitleSubclass`), one-shot per HWND.

## Console window

`ConsoleWndProc` (`Console.cpp`) owns the console's own window:

- ImGui's `ImGui_ImplWin32_WndProcHandler` runs first.
- `WM_INPUT` -> `HandleRawKeyboard` (console summon, below).
- `WM_GAME_TITLE_CHANGED` -> re-read and apply the game title.
- `WM_SYSCOMMAND` -> swallow `SC_KEYMENU` (Alt) so it does not eat REPL keys.
- `WM_KEYDOWN` `VK_HOME` -> hide the console (unless an ImGui text field wants
  the key).

### Console summon via raw input

The console is summoned with **Ctrl+Break**, detected through raw input
(`WM_INPUT`), not a low-level keyboard hook. `SetRawKeyboardInput` registers the
console window for keyboard raw input with `RIDEV_INPUTSINK`, so it receives key
events even when the game window has focus. `HandleRawKeyboard`:

- Reacts to key-down only (ignores `RI_KEY_BREAK`), on `VK_CANCEL` / `VK_PAUSE`
  with Ctrl held.
- Filters on the foreground game HWND, so with multiple d2bs instances only the
  focused one reacts (raw input is delivered per registering window).
- Is observe-only - it cannot consume the key, so Ctrl+Break also reaches the
  game (D2 ignores `VK_CANCEL`). It runs on the console thread, so it still fires
  if the game thread is hung.

## Threading

- **Game thread** owns the game window. `SetWindowsHookEx` ties a hook's
  lifetime to the installing thread, so `input::Install` posts the
  `WH_GETMESSAGE` registration to the game thread via `GameThread::Post` (the
  game thread lives for the process lifetime) and subclasses the WndProc from
  the calling thread. The 1.14d backend's `InstallWin32Hooks` captures
  `gameThreadId` from `GetWindowThreadProcessId` on the same window - the Sleep
  hook gates on it.
- **Console thread** owns the console window, its `ConsoleWndProc`, the raw-input
  registration, and the title subclass install.

## Lifecycle

`hooks::Install` (called once from the game callbacks wiring):

1. `InstallDetoursHooks` - `kernel32!Sleep`, the cursor-lock no-op, speedhack,
   and the SOCKS5 `connect` detour.
2. `intercepts::InstallAll` - inline mid-function patches (must precede the
   game's `WinMain`).
3. `InstallWin32Hooks` - polls for the game window, spawns the console
   (`console::Init`), captures `gameThreadId`, and calls `input::Install`,
   which posts the `WH_GETMESSAGE` registration to the game thread and
   subclasses the game WndProc.

`hooks::Remove` reverses it: `input::Remove` (unhook `WH_GETMESSAGE`, restore
the game WndProc), remove the inline patches, detach the Detours hooks. The
unhook has to run on the window's thread, which is the only one that runs
`GetMsgProc` and the subclassed WndProc, so neither can be mid-call while it
happens. `GameThread::Execute` cannot carry it there - by teardown the frontend
has shut its engine down and the per-frame drain no longer runs - so `Remove`
sends a private `WM_REMOVE_INPUT_HOOKS` (`WM_APP + 0x2B5`) with
`SendMessageTimeoutW`. If that thread does not answer within two seconds the
caller retires the hooks from its own thread instead. The
console unregisters raw input (`RIDEV_REMOVE` with a null target) and restores
its title subclass on teardown.
