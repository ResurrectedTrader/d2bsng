#include "game/Console.h"

#include <memory>
#include <vector>

#include "console/Console.h"
#include "hooks/HookManager.h"

// Backend-side game::console sink. OnMessage forwards to the frontend console via the
// registered onConsoleMessage callback; Show/Hide/Toggle forward to the host
// window. game:: is the backend-chosen sink - the frontend routes its
// ConsoleSink through here so a backend can intercept or redirect output.

namespace d2bs::game::console {

void OnMessage(const Message& msg) {
    if (const auto* callbacks = d2bs::hooks::GetActiveCallbacks();
        callbacks != nullptr && callbacks->onConsoleMessage != nullptr) {
        callbacks->onConsoleMessage(msg);
    }
}

void Show() {
    d2bs::console::Show();
}

void Hide() {
    d2bs::console::Hide();
}

void Toggle() {
    d2bs::console::Toggle();
}

bool IsVisible() {
    return d2bs::console::IsVisible();
}

std::vector<std::unique_ptr<BackendPanel>> GetBackendPanels() {
    return {};
}

}  // namespace d2bs::game::console
