#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2net {

// ---- Functions -------------------------------------------------------------
// Pointer args declared `const` even though the underlying game functions
// take non-const buffers; they never mutate them, and declaring const here
// lets callers pass `std::span<const uint8_t>::data()` without `const_cast`.
// Bit-pattern identical at the ABI boundary.
// MOO: CLIENT_Send(int32_t nUnused, const uint8_t* pBuffer, int32_t nBufferSize).
// 1.14d reorders the args; arg 2 is read but never used in the body.
inline StdcallFunc<void(size_t /*aLen*/, uint32_t /*nUnused*/, const uint8_t* /*aPacket*/)> CLIENT_Send{0x12AE50};
inline FastcallFunc<void(const uint8_t* /*aPacket*/, uint32_t /*aLen*/)> CLIENT_DequeueGamePacket{0x12AEB0};
// Intercept-only: reference calls this via inline `call D2NET_ReceivePacket_I`,
// the third arg is an out-pointer the function writes the parsed packet length
// to. Per the IDA decompile of sub_52B920.
inline FastcallFunc<void(const uint8_t* /*aPacket*/, uint32_t /*aLen*/, int32_t* /*pParsedLength*/)>
    CLIENT_ReceivePacket_I{0x12B920};

}  // namespace d2bs::imports::d2net
// NOLINTEND(readability-identifier-naming)
