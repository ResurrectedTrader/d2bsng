#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include "api/core/Class.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Sandbox native data - the sandbox context's global IS the inner object (matching d2bs reference)
struct SandboxData {
    ub::Context context;
    std::unordered_set<std::string> includedFiles;
};

// Sandbox class - provides an isolated JavaScript execution environment
// Dynamic properties: get/set/delete properties in the sandbox scope
// Methods: evaluate, include, isIncluded, clearScope
class JSSandbox : public ClassBase<JSSandbox, SandboxData> {
   public:
    static constexpr std::string_view ClassName = "Sandbox";

    // Constructor callback
    static std::unique_ptr<SandboxData> New(const ub::CallbackInfo& args);

    static void Configure(const ub::Class<SandboxData>& cls);

   private:
    // Named property interceptor on every Sandbox instance: property access is proxied to the
    // sandbox context's global object. The getter declines for a property the scope does not
    // define, so the prototype's methods stay reachable.
    static ub::Intercepted NamedPropertyGetter(const ub::Local<ub::Name>& property,
                                               const ub::PropertyCallbackInfo& info);
    static ub::Intercepted NamedPropertySetter(const ub::Local<ub::Name>& property, const ub::Local<ub::Value>& value,
                                               const ub::PropertyCallbackInfo& info);
    static std::optional<ub::PropertyAttribute> NamedPropertyQuery(const ub::Local<ub::Name>& property,
                                                                   const ub::PropertyCallbackInfo& info);
    static std::optional<bool> NamedPropertyDeleter(const ub::Local<ub::Name>& property,
                                                    const ub::PropertyCallbackInfo& info);
    static std::optional<ub::Local<ub::Array>> NamedPropertyEnumerator(const ub::PropertyCallbackInfo& info);
};

}  // namespace d2bs::api::classes
