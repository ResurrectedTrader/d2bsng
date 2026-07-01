#pragma once

#include <v8.h>
#include <string>
#include <unordered_set>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"

namespace d2bs::api::classes {

// Sandbox native data - the sandbox context's global IS the inner object (matching d2bs reference)
struct SandboxData {
    v8::Global<v8::Context> context;
    std::unordered_set<std::string> includedFiles;
};

// Sandbox class - provides an isolated JavaScript execution environment
// Dynamic properties: get/set/delete properties in the sandbox scope
// Methods: evaluate, include, isIncluded, clearScope
class JSSandbox : public V8ClassBase<JSSandbox, SandboxData> {
   public:
    static constexpr std::string_view ClassName = "Sandbox";

    // Constructor callback
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);

   private:
    // ========================================================================
    // Named Property Handlers (for dynamic property access)
    // V8 v14+ uses Intercepted return type for interceptor callbacks
    // ========================================================================

    // Getter for named properties
    static v8::Intercepted NamedPropertyGetter(v8::Local<v8::Name> property,
                                               const v8::PropertyCallbackInfo<v8::Value>& info);

    // Setter for named properties
    static v8::Intercepted NamedPropertySetter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                                               const v8::PropertyCallbackInfo<void>& info);

    // Query for named properties
    static v8::Intercepted NamedPropertyQuery(v8::Local<v8::Name> property,
                                              const v8::PropertyCallbackInfo<v8::Integer>& info);

    // Deleter for named properties
    static v8::Intercepted NamedPropertyDeleter(v8::Local<v8::Name> property,
                                                const v8::PropertyCallbackInfo<v8::Boolean>& info);

    // Enumerator for named properties (still returns void)
    static void NamedPropertyEnumerator(const v8::PropertyCallbackInfo<v8::Array>& info);
};

}  // namespace d2bs::api::classes
