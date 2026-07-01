#pragma once

#include <v8.h>

namespace d2bs::api::classes {

// Register all class constructors on the global object template
void RegisterAllClasses(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global);

// Clear all per-isolate template caches (call before isolate disposal)
void ClearAllClassCaches(v8::Isolate* isolate);

// Create the special 'me' global object (extended Unit representing the player)
v8::Local<v8::Object> CreateMeObject(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace d2bs::api::classes
