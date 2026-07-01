#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2lang {

// ---- Functions -------------------------------------------------------------
inline FastcallFunc<wchar_t*(uint16_t /*nLocaleTxtNo*/)> D2LANG_GetLocaleText{0x124A30};

// Not declared in this layer:
//   Say_II: reference's active Say_ASM jumps to D2CLIENT_Say_I instead, so
//     D2LANG_Say_II is dead code.

}  // namespace d2bs::imports::d2lang
// NOLINTEND(readability-identifier-naming)
