#pragma once

#include "scripting/Registry.h"

namespace d2bs::api::globals {

// Register the hash globals. Takes only the registry: these bindings name no
// engine, so there is no isolate or template to hand them.
void RegisterHashFunctions(script::Registry& registry);

}  // namespace d2bs::api::globals
