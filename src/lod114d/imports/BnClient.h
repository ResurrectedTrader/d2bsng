#pragma once

#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::bnclient {

// ---- Variables -------------------------------------------------------------
inline GameVar<char*> gpszClassicCdKey{0x482744};
inline GameVar<char*> gpszExpansionCdKey{0x48274C};
inline GameVar<char*> gpszKeyOwner{0x482750};

// ---- Interior addresses (intercept tail-targets) ---------------------------
// Tail-jump targets for the raw-CDKey injection intercepts. DClass lands in
// the 5-byte alternative path the original code's short JMP at 0x123671
// would otherwise skip (both branches converge at 0x123678). DLod is the
// sequential continuation immediately after the patched 5-byte MOV.
inline GameAsmFunc DClass{0x123673};
inline GameAsmFunc DLod{0x12395D};

}  // namespace d2bs::imports::bnclient
// NOLINTEND(readability-identifier-naming)
