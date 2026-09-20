#pragma once

#include "scripting/Registry.h"

namespace d2bs::api::globals {

// Register the menu globals. Takes only the registry: these bindings name no
// engine, so there is no isolate or template to hand them.
void RegisterMenuFunctions(script::Registry& registry);

}  // namespace d2bs::api::globals
