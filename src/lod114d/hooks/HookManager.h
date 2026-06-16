#pragma once

#include <Windows.h>

#include <optional>

#include "game/GameCallbacks.h"

// Hook subsystem. Owns:
//   * Detours-installed function-replacement hooks
//       - kernel32!Sleep (drives GameLoop ticks; gates on game thread)
//       - cursor-lock no-op
//   * Win32 hooks
//       - SetWindowsHookEx WH_GETMESSAGE (input dispatch + blockKeys/blockMouse,
//         pass-through of injected input)
//       - WndProc subclass            (WM_COPYDATA -> onIPC, captures game thread id)
//   * Inline-patch infrastructure (5-byte JMPs at game-side mid-function sites,
//     dispatch to naked-asm intercepts in src/lod114d/hooks/Intercepts.cpp).

namespace d2bs::hooks {

// Install every hook (Detours + Win32 + inline patches). Idempotent. Captures *callbacks by pointer - caller must
// ensure the storage outlives Remove().
//
// Win32 hooks need a game window; install only after D2GFX_GetHwnd() returns non-null.
void Install(const game::GameCallbacks* callbacks);

// Remove every hook. Idempotent. Reverses the order of Install().
void Remove();

// Returns the captured game-thread id, or std::nullopt until InstallWin32Hooks
// has captured the game window's owning thread. The Sleep hook uses this to
// gate drain-vs-passthrough.
std::optional<DWORD> GetGameThreadId();

// Accessor for the active callback table. Returns nullptr before Install() or after Remove().
const game::GameCallbacks* GetActiveCallbacks();

// Post synthetic input (clicks / keys) to the game window. Tagged so the input
// hook lets it through even while blockKeys / blockMouse suppresses the human's
// hardware input. Used by SendClick / SendKey / control clicks.
void PostInjectedInput(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

}  // namespace d2bs::hooks
