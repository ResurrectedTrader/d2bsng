#pragma once

#include <cstdint>

// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "components/script/ScriptTypesEnumNames.h"

namespace d2bs {

// Ordering matters: Stop() uses `>= Ready` to decide if the isolate
// exists and TerminateExecution is safe to call.  Do not reorder.
enum class ScriptState : uint8_t {
    Starting,  // Thread started, isolate being created
    Ready,     // Isolate created, safe for cross-thread TerminateExecution
    Running,   // Executing JavaScript
    Paused,    // Temporarily suspended
    Stopping,  // Stop requested, cleanup in progress
    Stopped    // Finished, thread exited
};

enum class ScriptMode : uint8_t {
    InGame,     // Runs when player is in game
    OutOfGame,  // Runs at menu/lobby
    Console     // Console script (persistent, event-driven)
};

// Which of a drawable's two input callbacks a Script accessor addresses.
enum class DrawableHandler : uint8_t { Click, Hover };

}  // namespace d2bs
