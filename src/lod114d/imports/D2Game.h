#pragma once

#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2game {

// ---- Functions -------------------------------------------------------------
// The d2bs hook subsystem reads the resolved address to install its hook on
// the game exit path.
inline FastcallFunc<uint32_t()> D2GAME_Exit{0x576F};

}  // namespace d2bs::imports::d2game
// NOLINTEND(readability-identifier-naming)
