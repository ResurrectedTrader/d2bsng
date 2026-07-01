#pragma once

#include "D2MOOConfig.h"
#include "ImportTypes.h"
#include "extras/WindowHandlers.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::storm {

// ---- Variables -------------------------------------------------------------
inline GameVar<extras::WindowHandlerHashTable> gWindowHandlers{0x379300};

}  // namespace d2bs::imports::storm
// NOLINTEND(readability-identifier-naming)
