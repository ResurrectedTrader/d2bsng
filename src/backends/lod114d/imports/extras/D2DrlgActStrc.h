#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2DrlgActStrc` has different offsets
// for 1.14d (D2MOO was reverse-engineered against 1.10c). Reference d2bs's
// CODE reads these fields at the bytes pinned here, and reference works on
// 1.14d. Use this struct via `d2bs::imports::extras::D2DrlgActStrc` or
// via `using d2bs::imports::extras::D2DrlgActStrc` to shadow D2MOO's
// version inside the consuming TU.
//
// D2MOO's claimed size is 0x60 with pRoom @ 0x04 / pDrlg @ 0x08 /
// dwInitSeed @ 0x0C. Reference Act is 0x4C with dwMapSeed @ 0x0C /
// pRoom1 @ 0x10 / dwAct @ 0x14 / pMisc @ 0x48. We model the 1.14d
// allocation here.
//
// Field naming follows D2MOO (`pRoom`, `pDrlg`, `dwAct`); offsets follow
// reference. The `pDrlg` field at 0x48 reaches what D2MOO calls
// D2DrlgStrc but with a different layout - see
// d2bs::imports::extras::D2DrlgStrc.

namespace d2bs::imports::extras {

struct D2ActiveRoomStrc;
struct D2DrlgStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2DrlgActStrc {
    std::array<uint32_t, 3> _1;   // 0x00 - opaque (reference Act._1[3])
    uint32_t dwMapSeed;           // 0x0C - reference Act::dwMapSeed
    D2ActiveRoomStrc* pRoom;      // 0x10 - reference Act::pRoom1
    uint32_t dwAct;               // 0x14 - reference Act::dwAct
    std::array<uint32_t, 12> _2;  // 0x18 - opaque (reference Act._2[12])
    D2DrlgStrc* pDrlg;            // 0x48 - reference Act::pMisc; reaches the 1.14d ActMisc-shaped allocation
};
// NOLINTEND(readability-identifier-naming)

static_assert(sizeof(D2DrlgActStrc) == 0x4C, "D2DrlgActStrc must be 0x4C bytes (1.14d)");
static_assert(offsetof(D2DrlgActStrc, dwMapSeed) == 0x0C, "D2DrlgActStrc::dwMapSeed offset drift");
static_assert(offsetof(D2DrlgActStrc, pRoom) == 0x10, "D2DrlgActStrc::pRoom offset drift");
static_assert(offsetof(D2DrlgActStrc, dwAct) == 0x14, "D2DrlgActStrc::dwAct offset drift");
static_assert(offsetof(D2DrlgActStrc, pDrlg) == 0x48, "D2DrlgActStrc::pDrlg offset drift");

}  // namespace d2bs::imports::extras
