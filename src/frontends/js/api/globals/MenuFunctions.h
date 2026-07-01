#pragma once

#include <v8.h>

namespace d2bs::api::globals {

// Register all menu/OOG functions and timing/event functions on the global object
void RegisterMenuFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global);

}  // namespace d2bs::api::globals
