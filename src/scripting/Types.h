#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Vocabulary a scripting engine produces, and nothing else.
//
// The test for anything proposed here is whether an engine is what answers it.
// A script's name, its mode, the files it has included, the handlers it has
// registered, how often it should capture a stack - none of those come from an
// engine, so none of them belong in this package, however much they look like
// they are "about scripting". They are runtime bookkeeping that happens to
// live near a script.
namespace d2bs {

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

// What the engine behind this frontend is, and what it can do. The console asks
// instead of assuming: it names the engine it is actually running on, and draws
// a section for a capability only where the engine reports it. Fixed for the
// life of the process.
struct EngineInfo {
    std::string_view name;  // as a user should see it, e.g. "V8"
    std::string version;
    bool inspector = false;  // attach a remote debugger (Chrome DevTools)
};

// Which of a drawable's two input callbacks a Script accessor addresses.

// renderer substitutes placeholders ("<anonymous>" / "<unknown>") at draw
// time so the data model stays raw.
struct StackFrame {
    std::string functionName;
    std::string scriptName;
    int32_t line = 0;
    int32_t column = 0;
};

// Captured JS call stack of a script's isolate. Produced by
// the engine capturing it (see StackCaptureMode for when);
// read cross-thread via the script that owns it.
struct StackTraceSnapshot {
    std::vector<StackFrame> frames;
};

}  // namespace d2bs
