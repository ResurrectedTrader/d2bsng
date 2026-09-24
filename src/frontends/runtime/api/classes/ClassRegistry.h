#pragma once

#include <optional>

#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Install every class constructor on the context's global object. False if any could not be
// installed, with the engine's exception (if it raised one) pending.
bool RegisterAllClasses(const ub::Context& context);

// Forget this thread's declared classes (call while the isolate is torn down).
void ClearAllClassCaches();

// Create the special 'me' global object (extended Unit representing the player)
std::optional<ub::Local<ub::Object>> CreateMeObject(const ub::Context& context);

}  // namespace d2bs::api::classes
