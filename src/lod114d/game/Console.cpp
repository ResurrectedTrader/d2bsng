#include "game/Console.h"

#include "components/console/Console.h"
#include "console/Console.h"

// Port-side game::console sink. OnMessage forwards to the framework console;
// Show/Hide/Toggle forward to the host window. The game->framework direction
// is intentional - game:: is the port-chosen sink, not a layering bug.

namespace d2bs::game::console {

void OnMessage(const Message& msg) {
    d2bs::framework::console::OnMessage(msg);
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

}  // namespace d2bs::game::console
