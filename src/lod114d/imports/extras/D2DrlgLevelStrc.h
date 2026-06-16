#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2DrlgLevelStrc` has different offsets
// for 1.14d (D2MOO was reverse-engineered against 1.10c). Reference d2bs's
// CODE reads these fields at the bytes pinned here, and reference works on
// 1.14d. Use this struct via `d2bs::imports::extras::D2DrlgLevelStrc` or
// via `using d2bs::imports::extras::D2DrlgLevelStrc` to shadow D2MOO's
// version inside the consuming TU.
//
// D2MOO's claimed size is 0x230 with pDrlg @ 0x00 / nLevelId @ 0x04 /
// pFirstRoomEx @ 0x30 / pNextLevel @ 0x22C. Reference Level is 0x22C with
// 0x10 of opaque pad at 0x00, then pRoom2First @ 0x10, dwPosX @ 0x1C,
// dwLevelNo @ 0x1D0, pNextLevel @ 0x1AC, etc. We model the 1.14d
// allocation here.
//
// Field naming follows D2MOO (`pFirstRoomEx`, `nPosX/Y`, `nWidth/Height`,
// `pNextLevel`, `pDrlg`, `nLevelId`); offsets follow reference. The
// `pDrlg` field at 0x1B4 reaches what D2MOO calls D2DrlgStrc but with a
// different layout - see d2bs::imports::extras::D2DrlgStrc.

namespace d2bs::imports::extras {

struct D2DrlgRoomStrc;
struct D2DrlgStrc;
struct D2DrlgLevelStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2DrlgLevelStrc {
    std::array<uint32_t, 4> _1;    // 0x000 - opaque (reference Level._1[4])
    D2DrlgRoomStrc* pFirstRoomEx;  // 0x010 - reference Level::pRoom2First
    std::array<uint32_t, 2> _2;    // 0x014 - opaque (reference Level._2[2])
    int32_t nPosX;                 // 0x01C - reference Level::dwPosX
    int32_t nPosY;                 // 0x020 - reference Level::dwPosY
    int32_t nWidth;                // 0x024 - reference Level::dwSizeX
    int32_t nHeight;               // 0x028 - reference Level::dwSizeY
    std::array<uint32_t, 96> _3;   // 0x02C - opaque (reference Level._3[96])
    D2DrlgLevelStrc* pNextLevel;   // 0x1AC - reference Level::pNextLevel
    uint32_t _4;                   // 0x1B0 - opaque (reference Level._4)
    D2DrlgStrc* pDrlg;  // 0x1B4 - reference Level::pMisc; reaches the 1.14d ActMisc-shaped allocation that we model as
                        // d2bs::imports::extras::D2DrlgStrc
    std::array<uint32_t, 6> _5;               // 0x1B8 - opaque (reference Level._5[6])
    int32_t nLevelId;                         // 0x1D0 - reference Level::dwLevelNo
    std::array<uint32_t, 3> _6;               // 0x1D4 - opaque (reference Level._6[3])
    std::array<int32_t, 9> nRoomCenterWarpX;  // 0x1E0 - reference Level::RoomCenterX/WarpX[9] union
    std::array<int32_t, 9> nRoomCenterWarpY;  // 0x204 - reference Level::RoomCenterY/WarpY[9] union
    uint32_t dwRoomEntries;                   // 0x228 - reference Level::dwRoomEntries
};
// NOLINTEND(readability-identifier-naming)

static_assert(sizeof(D2DrlgLevelStrc) == 0x22C, "D2DrlgLevelStrc must be 0x22C bytes (1.14d)");
static_assert(offsetof(D2DrlgLevelStrc, pFirstRoomEx) == 0x010, "D2DrlgLevelStrc::pFirstRoomEx offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nPosX) == 0x01C, "D2DrlgLevelStrc::nPosX offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nPosY) == 0x020, "D2DrlgLevelStrc::nPosY offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nWidth) == 0x024, "D2DrlgLevelStrc::nWidth offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nHeight) == 0x028, "D2DrlgLevelStrc::nHeight offset drift");
static_assert(offsetof(D2DrlgLevelStrc, pNextLevel) == 0x1AC, "D2DrlgLevelStrc::pNextLevel offset drift");
static_assert(offsetof(D2DrlgLevelStrc, pDrlg) == 0x1B4, "D2DrlgLevelStrc::pDrlg offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nLevelId) == 0x1D0, "D2DrlgLevelStrc::nLevelId offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nRoomCenterWarpX) == 0x1E0, "D2DrlgLevelStrc::nRoomCenterWarpX offset drift");
static_assert(offsetof(D2DrlgLevelStrc, nRoomCenterWarpY) == 0x204, "D2DrlgLevelStrc::nRoomCenterWarpY offset drift");
static_assert(offsetof(D2DrlgLevelStrc, dwRoomEntries) == 0x228, "D2DrlgLevelStrc::dwRoomEntries offset drift");

}  // namespace d2bs::imports::extras
