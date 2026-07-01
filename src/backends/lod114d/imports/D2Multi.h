#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <array>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2multi {

// ---- Functions -------------------------------------------------------------
inline FastcallFunc<void()> D2MULTI_DoChat{0x42810};

// ---- Variables -------------------------------------------------------------
inline GameVar<std::array<char, 512>> gszChatBoxMsg{0x37AE40};

// ---- Inline-patch intercept tail-call targets ------------------------------
// Hook subsystem (`src/backends/lod114d/hooks/Intercepts.cpp`) needs the original target
// of the CALL it overwrites at 0x442A61 so the naked thunk can tail-call
// after dispatching the C handler.
inline GameAsmFunc ChannelInput_I{0x428D0};

// Not declared in this layer:
//   ChannelChat_I, ChannelEmote_I, ChannelWhisper_I: reference's patch lines
//     for these are commented out (1.13d-era code).

}  // namespace d2bs::imports::d2multi
// NOLINTEND(readability-identifier-naming)
