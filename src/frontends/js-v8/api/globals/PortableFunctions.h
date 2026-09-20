#pragma once

#include "scripting/Registry.h"

namespace d2bs::api::globals {

// Globals written against the scripting contract instead of against V8. The
// implementation names no engine type, so a second frontend runs the same
// bindings by implementing d2bs::script rather than rewriting them.
void RegisterPortableFunctions(script::Registry& registry);

}  // namespace d2bs::api::globals
