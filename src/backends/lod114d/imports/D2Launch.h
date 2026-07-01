#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include "extras/BnetData.h"

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2launch {

// ---- Variables -------------------------------------------------------------
inline GameVar<extras::BnetData*> gpBnetData{0x3795D4};

}  // namespace d2bs::imports::d2launch
// NOLINTEND(readability-identifier-naming)
