#pragma once

namespace d2bs::console {

// Spawn the console host window and render thread. Idempotent; safe to call multiple times.
void Init();

// No-ops until Init() has produced an HWND.
void Show();
void Hide();
void Toggle();

}  // namespace d2bs::console
