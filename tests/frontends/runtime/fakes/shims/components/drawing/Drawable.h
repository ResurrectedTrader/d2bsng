#pragma once

// Test-only shim for src/frontends/runtime/components/drawing/Drawable.h.
//
// The real DrawAll walks every script's drawables through ScriptEngine.
// GameLoop.cpp only calls Drawable::DrawAll with a GameState, so the shim
// exposes just that entry point.

#include "game/Types.h"

namespace d2bs::runtime::drawing {

struct Drawable {
    static void DrawAll(d2bs::game::GameState state);
};

}  // namespace d2bs::runtime::drawing
