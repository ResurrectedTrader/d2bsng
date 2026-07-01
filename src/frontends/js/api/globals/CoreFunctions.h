#pragma once

#include <v8.h>

namespace d2bs::api::globals {

// Register all core global functions on the global object template.
void RegisterCoreFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global);

}  // namespace d2bs::api::globals
