#pragma once

#include <v8.h>

namespace d2bs::api::globals {

// Register all hash functions on the global object template
void RegisterHashFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global);

}  // namespace d2bs::api::globals
