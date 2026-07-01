# Window message and input handling

How d2bs intercepts and injects Windows messages for the 1.14d port. Two OS
windows are involved:

- **Game window** - D2's main window, owned by the game (render) thread.
- **Console window** - the d2bs dev console, owned by the console render thread
  (spawned by `console::Init`).

All of the game-window machinery lives in `src/backends/lod114d/hooks/HookManager.cpp`;
the console window lives in `src/backends/lod114d/console/Console.cpp`.

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

Dispatch path (all in `HookManager.cpp`):

- `GetMsgProc` routes `WM_MOUSEFIRST..WM_MOUSELAST` to `ProcessMouse` and
  `WM_KEYFIRST..WM_KEYLAST` to `ProcessKey`.
- `ProcessMouse` / `ProcessKey` first honor the injected-input tag (below), then
  call `HandleMouseMessage` / `HandleKeyMessage`, which dispatch to the
  framework callbacks (`onMouseClick`, `onMouseMove`, `onKeyEvent`) and return
  whether to block. A block (from `blockMouse` / `blockKeys`, or a callback that
  consumes the event) results in `Neutralize`.
- Character messages (`WM_CHAR` and friends) always pass through; when
  `blockKeys` is set, the originating key-down was already neutralized before
  `TranslateMessage` ran, so any character left in the stream is our own.

## Human vs injected input (tagging)

The same hook must let the bot's own synthetic clicks / keystrokes through even
while `blockKeys` / `blockMouse` is suppressing the human at the keyboard.
Injected messages are tagged in bits that real hardware leaves clear, and the
tag is stripped before the game sees the message:

- Mouse: `INJECTED_MOUSE_TAG` in `HIWORD(wParam)` (unused for the button
  messages we post).
- Key: `INJECTED_KEY_TAG` in `lParam` bit 25 (a reserved keystroke-flag bit,
  zero on real input).

`PostInjectedInput(hwnd, message, wParam, lParam)` (public in `HookManager.h`)
applies the right tag and `PostMessageW`s the message. `GetMsgProc` sees the
tag, clears it, and lets the message reach the game unblocked and without
re-dispatching it as a human event.

Callers:

- `game::SendClick` / `game::SendKey` (`game/GameHelpers.cpp`), exposed to
  scripts via the `sendClick` / `sendKey` globals (`api/globals/CoreFunctions.cpp`).
- Control clicks (`game/Control.cpp`).

## Game-window WndProc subclasses

Two subclasses sit on the game HWND. Each saves the previous `WNDPROC` and
chains to it with `CallWindowProcW`, so order does not matter:

1. `SubclassedWndProc` (`HookManager.cpp`) - handles `WM_COPYDATA` and forwards
   the payload to `onIPC` (the launcher / inter-instance IPC channel; matches the
   reference launcher's `CF_TEXT`-style null-terminated convention). Everything
   else chains through.
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
  lifetime to the installing thread, so `InstallWin32Hooks` posts the
  `WH_GETMESSAGE` registration to the game thread via `GameThread::Post` (the
  game thread lives for the process lifetime). The `WM_COPYDATA` subclass is set
  from there too, and `gameThreadId` is captured from
  `GetWindowThreadProcessId` - the Sleep hook gates on it.
- **Console thread** owns the console window, its `ConsoleWndProc`, the raw-input
  registration, and the title subclass install.

## Lifecycle

`hooks::Install` (called once from the game callbacks wiring):

1. `InstallDetoursHooks` - `kernel32!Sleep`, the cursor-lock no-op, speedhack,
   and the SOCKS5 `connect` detour.
2. `intercepts::InstallAll` - inline mid-function patches (must precede the
   game's `WinMain`).
3. `InstallWin32Hooks` - polls for the game window, spawns the console
   (`console::Init`), captures `gameThreadId`, posts the `WH_GETMESSAGE`
   registration to the game thread, and subclasses the game WndProc.

`hooks::Remove` reverses it: unhook `WH_GETMESSAGE`, restore the game WndProc,
remove the inline patches, detach the Detours hooks. The console unregisters raw
input (`RIDEV_REMOVE` with a null target) and restores its title subclass on
teardown.
