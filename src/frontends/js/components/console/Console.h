#pragma once

#include "game/Console.h"

namespace d2bs::js::console {

// One-per-frame draw call. Always drains the cross-thread message queue, then
// renders the tab bar and panels only while the console is visible (queried via
// game::console::IsVisible). On the visible->hidden edge it clears every script's
// stack capture - a script left selected in the Stacktraces panel would otherwise
// keep walking its V8 stack at every delay(); the panel re-enables the selected
// script on show. The host (port-side) invokes this between ImGui::NewFrame and
// ImGui::Render with an ImGui context active.
void DrawFrame();

// Any-thread message intake. Routed from each port's
// d2bs::game::console::OnMessage shim. Bounded - messages drop the oldest
// entries when the queue grows past the limit.
void OnMessage(const game::console::Message& msg);

}  // namespace d2bs::js::console
