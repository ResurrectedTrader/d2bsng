#pragma once

// Test-only shim for src/framework/components/drawing/Drawable.h.
//
// The real header pulls in <v8.h> via the Drawable struct (Global handles
// for onClick/onHover, collection operations that iterate ScriptEngine).
// GameLoop.cpp only calls Drawable::DrawAll with a GameState, so the shim
// exposes just that entry point.

#include "game/Types.h"

namespace d2bs::framework::drawing {

struct Drawable {
    static void DrawAll(d2bs::game::GameState state);
};

}  // namespace d2bs::framework::drawing
