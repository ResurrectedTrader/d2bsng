#pragma once

#include "unibind/unibind.h"

namespace d2bs::runtime::api::globals {

// Register all game functions on the context's global object
void RegisterGameFunctions(const ub::Context& context);

}  // namespace d2bs::runtime::api::globals
