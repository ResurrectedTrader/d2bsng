#pragma once

#include "components/script/NativeCallHook.h"
#include "unibind/unibind.h"

namespace d2bs::api::function {

// Install a global function on the context's global object. Goes through the NativeCallHook
// trampoline so per-call stack capture (toggled per script in the console) and the Profiling panel
// see every global-function entry. False if the function could not be made or installed.
inline bool Register(const ub::Context& context, const char* name, ub::FunctionCallback callback) {
    auto* binding = runtime::script::InternFunction(name, callback);
    auto function = ub::Function::New(context, &runtime::script::MethodTrampoline, ub::CallbackData::For(*binding));
    return function && context.GlobalObject().Set(context, name, *function).value_or(false);
}

}  // namespace d2bs::api::function
