#pragma once

#include "unibind/unibind.h"

namespace d2bs::api::globals {

// Register all core global functions on the context's global object.
void RegisterCoreFunctions(const ub::Context& context);

}  // namespace d2bs::api::globals
