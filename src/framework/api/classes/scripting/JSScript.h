#pragma once

#include <v8.h>
#include <cstdint>
#include <string>
#include <thread>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "components/script/ScriptEngine.h"

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
class JSScript : public V8ClassBase<JSScript, ScriptHandle> {
   public:
    static constexpr std::string_view ClassName = "D2BSScript";

    // Script objects are obtained via getScript()/getScripts() global functions, not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);

    // Create a Script JS object from a Script pointer
    // Returns empty handle if script is null
    static v8::Local<v8::Object> Create(v8::Isolate* isolate, d2bs::Script* script);
};

}  // namespace d2bs::api::classes
