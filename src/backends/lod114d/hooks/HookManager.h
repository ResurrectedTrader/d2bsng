#pragma once

#include <Windows.h>

#include <optional>

#include "game/GameCallbacks.h"

// Hook subsystem. Owns:
//   * Detours-installed function-replacement hooks
//       - kernel32!Sleep (drives GameLoop ticks; gates on game thread)
//       - cursor-lock no-op
//   * the game-window input hook and WM_COPYDATA subclass (core/input/InputHook),
//     installed once the game window exists, and the game thread id captured
//     from that window
//   * Inline-patch infrastructure (5-byte JMPs at game-side mid-function sites,
//     dispatch to naked-asm intercepts in src/backends/lod114d/hooks/Intercepts.cpp).

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

}  // namespace d2bs::hooks
