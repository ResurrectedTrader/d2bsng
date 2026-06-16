#pragma once

#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2cmp {

// ---- Functions -------------------------------------------------------------
inline StdcallFunc<void(void* /*File*/, void** /*Out*/, const char* /*SourceFile*/, uint32_t /*Line*/,
                        uint32_t /*FileVersion*/, const char* /*Filename*/)>
    D2CMP_InitCellFile{0x201340};

}  // namespace d2bs::imports::d2cmp
// NOLINTEND(readability-identifier-naming)
