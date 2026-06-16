#pragma once

#include <v8.h>

#include "components/script/NativeCallHook.h"

// Helper utilities for registering global functions

namespace d2bs::api::v8_function {

// Register a global function on the global object. Goes through the
// framework's MethodTrampoline so per-callback stack capture (toggled per
// script in the console) sees every global-function entry.
inline void Register(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global, const char* name,
                     v8::FunctionCallback callback) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - function-pointer through External void*
    auto data = v8::External::New(isolate, reinterpret_cast<void*>(callback));
    global->Set(isolate, name, v8::FunctionTemplate::New(isolate, &framework::script::MethodTrampoline, data));
}

}  // namespace d2bs::api::v8_function
