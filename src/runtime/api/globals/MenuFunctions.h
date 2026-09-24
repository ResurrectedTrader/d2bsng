#pragma once

#include "unibind/unibind.h"

namespace d2bs::runtime::api::globals {

// Register all menu/OOG functions and timing/event functions on the global object
void RegisterMenuFunctions(const ub::Context& context);

}  // namespace d2bs::runtime::api::globals
