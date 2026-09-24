#pragma once

#include "unibind/unibind.h"

namespace d2bs::api::globals {

// Register all hash functions on the context's global object
void RegisterHashFunctions(const ub::Context& context);

}  // namespace d2bs::api::globals
