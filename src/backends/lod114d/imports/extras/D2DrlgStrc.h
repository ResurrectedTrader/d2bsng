#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2DrlgStrc` has different offsets for
// 1.14d (D2MOO was reverse-engineered against 1.10c, and the 1.11+ arm
// was reordered to counter maphack). Reference d2bs's CODE reads these
// fields at the bytes pinned here, and reference works on 1.14d. Use
// this struct via `d2bs::imports::extras::D2DrlgStrc` or via
// `using d2bs::imports::extras::D2DrlgStrc` to shadow D2MOO's version
// inside the consuming TU.
//
// D2MOO's 1.11+ arm claims size 0x488 with pAct @ 0x84 / pLevel @ 0x470 /
// nStaffTombLevel @ 0x478. Reference's ActMisc has dwStaffTombLevel @ 0x94,
// pAct @ 0x46C, pLevelFirst @ 0x47C - all on different bytes than D2MOO.
// We model the 1.14d allocation here.
//
// Reached via reference-correct chain: `Act@0x48 -> ActMisc` and
// `Level@0x1B4 -> ActMisc`. The role corresponds to D2MOO's D2DrlgStrc;
// the named pointer fields use D2MOO's names (`pAct`, `pLevel`,
// `nStaffTombLevel`) at reference's offsets.

namespace d2bs::imports::extras {

struct D2DrlgActStrc;
struct D2DrlgLevelStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2DrlgStrc {
    std::array<uint32_t, 37> _1;   // 0x000 - opaque (reference ActMisc._1[37])
    int32_t nStaffTombLevel;       // 0x094 - reference ActMisc::dwStaffTombLevel
    std::array<uint32_t, 245> _2;  // 0x098 - opaque (reference ActMisc._2[245])
    D2DrlgActStrc* pAct;           // 0x46C - reference ActMisc::pAct
    std::array<uint32_t, 3> _3;    // 0x470 - opaque (reference ActMisc._3[3])
    D2DrlgLevelStrc* pLevel;       // 0x47C - reference ActMisc::pLevelFirst
};
// NOLINTEND(readability-identifier-naming)

// Reference ActMisc's last named field is pLevelFirst @ 0x47C (4 bytes) so the
// modeled extent is 0x480. The actual game allocation is at least this size;
// fields past 0x480 (if any) are not exercised by 1.14d-port code.
static_assert(sizeof(D2DrlgStrc) == 0x480, "D2DrlgStrc must be 0x480 bytes (1.14d)");
static_assert(offsetof(D2DrlgStrc, nStaffTombLevel) == 0x094, "D2DrlgStrc::nStaffTombLevel offset drift");
static_assert(offsetof(D2DrlgStrc, pAct) == 0x46C, "D2DrlgStrc::pAct offset drift");
static_assert(offsetof(D2DrlgStrc, pLevel) == 0x47C, "D2DrlgStrc::pLevel offset drift");

}  // namespace d2bs::imports::extras
