#pragma once

namespace d2bs::lod114d::console {

// Spawn the console host window and render thread. Idempotent; safe to call multiple times.
void Init();

// No-ops until Init() has produced an HWND.
void Show();
void Hide();
void Toggle();

// Whether the host window is currently shown (false before Init()).
bool IsVisible();

}  // namespace d2bs::lod114d::console
