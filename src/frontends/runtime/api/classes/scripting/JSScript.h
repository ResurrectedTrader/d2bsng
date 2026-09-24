#pragma once

#include <string>
#include <thread>
#include "api/core/Class.h"
#include "components/script/ScriptEngine.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Script handle stored in JS wrapper - uses thread ID to safely reference script
// This prevents dangling pointers when script A tries to access script B that was destroyed
struct ScriptHandle {
    std::thread::id threadId;
};

// Script class - represents a running script instance
// Properties: name, type, running, threadid, memory
// Methods: getNext, pause, resume, join, stop, send
//
// Reference implementation notes:
// - type returns boolean (true = out-of-game, false = in-game)
// - getNext returns true if moved to next script, undefined if at end
// - All methods return null on success (matching JSVAL_NULL)
// - If script is not found, properties return undefined, methods return null
//
// Script objects are obtained via getScript()/getScripts() global functions (Wrap), not direct
// construction.
class JSScript : public ClassBase<JSScript, ScriptHandle> {
   public:
    static constexpr std::string_view ClassName = "D2BSScript";

    static void Configure(const ub::Class<ScriptHandle>& cls);
};

}  // namespace d2bs::api::classes
