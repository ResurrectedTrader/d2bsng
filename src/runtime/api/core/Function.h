#pragma once

#include <v8.h>

#include "components/script/NativeCallHook.h"

// Helper utilities for registering global functions

namespace d2bs::runtime::api::function {

// Register a global function on the global object. Goes through the
// framework's MethodTrampoline so per-callback stack capture (toggled per
// script in the console) sees every global-function entry.
inline void Register(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global, const char* name,
                     v8::FunctionCallback callback) {
    auto data = v8::External::New(isolate, script::InternFunction(name, callback), v8::kExternalPointerTypeTagDefault);
    global->Set(isolate, name, v8::FunctionTemplate::New(isolate, &script::MethodTrampoline, data));
}

}  // namespace d2bs::runtime::api::function
