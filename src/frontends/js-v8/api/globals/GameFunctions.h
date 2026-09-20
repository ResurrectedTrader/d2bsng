#pragma once

#include <v8.h>

#include "scripting/Registry.h"

namespace d2bs::api::globals {

// Register all game functions on the global object template
void RegisterGameFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global, script::Registry& registry);

}  // namespace d2bs::api::globals
