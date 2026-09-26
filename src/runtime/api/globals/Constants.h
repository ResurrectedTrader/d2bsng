#pragma once

#include "config/ProfileData.h"
#include "unibind/unibind.h"
// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "RuntimeEnumNames.h"

namespace d2bs::runtime::api::globals {

enum class FileMode : int32_t { Read = 0, Write = 1, Append = 2 };

using config::ProfileType;

// Register constants as global variables
void RegisterConstants(const ub::Context& context);

}  // namespace d2bs::runtime::api::globals
