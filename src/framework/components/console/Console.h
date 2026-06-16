#pragma once

#include "game/Console.h"

namespace d2bs::framework::console {

// One-per-frame draw call. The host (port-side) invokes this between
// ImGui::NewFrame and ImGui::Render. Opens a full-viewport root window,
// draws the tab bar with every registered panel, and pins a REPL input
// field to the bottom. Lazy-inits the panel registry on first call;
// drains the cross-thread message queue into LogPanel on each call.
//
// Caller must have an ImGui context active and a render-target bound.
void DrawFrame();

// Any-thread message intake. Routed from each port's
// d2bs::game::console::OnMessage shim. Bounded - messages drop the oldest
// entries when the queue grows past the limit.
void OnMessage(const d2bs::game::console::Message& msg);

}  // namespace d2bs::framework::console
