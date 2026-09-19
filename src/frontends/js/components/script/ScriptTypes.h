#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

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

// Memory figures for one script, in a form that names no engine type.
// Every field is optional because engines measure different things: a figure
// an engine cannot report stays empty, and the console renders the absence
// rather than a zero that would read as a measurement.
struct HeapStats {
    std::optional<uint64_t> used;
    std::optional<uint64_t> committed;
    std::optional<uint64_t> limit;
    std::optional<uint64_t> physical;
    std::optional<uint64_t> external;
    std::optional<uint64_t> peakMalloced;
    std::optional<uint64_t> usedHandles;
    std::optional<uint64_t> totalHandles;
};

// Live native wrapper objects owned by one script, keyed by script-visible
// class name. Transparent comparator so lookups take a string_view.
using ObjectCounts = std::map<std::string, int32_t, std::less<>>;

// Which of a drawable's two input callbacks a Script accessor addresses.
enum class DrawableHandler : uint8_t { Click, Hover };

}  // namespace d2bs
