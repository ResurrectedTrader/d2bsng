#pragma once

#include "D2RoomTileStrc.h"

#include <array>
#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2DrlgRoomStrc` has different offsets for
// 1.14d (D2MOO was reverse-engineered against 1.10c). Reference d2bs's CODE
// reads these fields at the bytes pinned here, and reference works on 1.14d.
// Use this struct via `d2bs::imports::extras::D2DrlgRoomStrc` or via
// `using d2bs::imports::extras::D2DrlgRoomStrc` to shadow D2MOO's version
// inside the consuming TU.
//
// D2MOO's claimed size is 0xEC. Reference Room2 ends at 0x60. We model the
// 1.14d allocation, which matches reference's reads.
//
// Field naming follows D2MOO (e.g. `pRoom` not reference's `pRoom1`,
// `pPresetUnits` not `pPreset`, `dwFlags` not `dwRoomFlags`); offsets follow
// reference (1.14d ground truth). Where reference has unnamed `_N` pads,
// the bytes are kept opaque rather than mapped to D2MOO's claimed-field
// names there - D2MOO disagrees about what's in those bytes for 1.14d.

struct D2DrlgPresetRoomStrc;
struct D2DrlgOutdoorRoomStrc;

namespace d2bs::imports::extras {

struct D2ActiveRoomStrc;
struct D2DrlgLevelStrc;
struct D2PresetUnitStrc;
struct D2DrlgRoomStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2DrlgRoomStrc {
    std::array<uint32_t, 2> _1;             // 0x00 - opaque (reference Room2._1[2])
    D2DrlgRoomStrc** ppRoomsNear;           // 0x08 - reference Room2::pRoom2Near
    std::array<uint32_t, 5> _2;             // 0x0C - opaque (reference Room2._2[5])
    union {                                 // 0x20 - reference Room2::pType2Info
        ::D2DrlgPresetRoomStrc* pMaze;      // D2MOO union arm name
        ::D2DrlgOutdoorRoomStrc* pOutdoor;  // D2MOO union arm name
    };
    D2DrlgRoomStrc* pDrlgRoomNext;   // 0x24 - reference Room2::pRoom2Next
    uint32_t dwFlags;                // 0x28 - reference Room2::dwRoomFlags
    int32_t nRoomsNear;              // 0x2C - reference Room2::dwRoomsNear
    D2ActiveRoomStrc* pRoom;         // 0x30 - reference Room2::pRoom1
    int32_t nTileXPos;               // 0x34 - reference Room2::dwPosX
    int32_t nTileYPos;               // 0x38 - reference Room2::dwPosY
    int32_t nTileWidth;              // 0x3C - reference Room2::dwSizeX
    int32_t nTileHeight;             // 0x40 - reference Room2::dwSizeY
    uint32_t _3;                     // 0x44 - opaque (reference Room2._3)
    int32_t nType;                   // 0x48 - reference Room2::dwPresetType
    D2RoomTileStrc* pRoomTiles;      // 0x4C - reference Room2::pRoomTiles
    std::array<uint32_t, 2> _4;      // 0x50 - opaque (reference Room2._4[2])
    D2DrlgLevelStrc* pLevel;         // 0x58 - reference Room2::pLevel
    D2PresetUnitStrc* pPresetUnits;  // 0x5C - reference Room2::pPreset
};
// NOLINTEND(readability-identifier-naming)

static_assert(sizeof(D2DrlgRoomStrc) == 0x60, "D2DrlgRoomStrc must be 0x60 bytes (1.14d)");
static_assert(offsetof(D2DrlgRoomStrc, ppRoomsNear) == 0x08, "D2DrlgRoomStrc::ppRoomsNear offset drift");
static_assert(offsetof(D2DrlgRoomStrc, pMaze) == 0x20, "D2DrlgRoomStrc::pMaze offset drift");
static_assert(offsetof(D2DrlgRoomStrc, pDrlgRoomNext) == 0x24, "D2DrlgRoomStrc::pDrlgRoomNext offset drift");
static_assert(offsetof(D2DrlgRoomStrc, dwFlags) == 0x28, "D2DrlgRoomStrc::dwFlags offset drift");
static_assert(offsetof(D2DrlgRoomStrc, nRoomsNear) == 0x2C, "D2DrlgRoomStrc::nRoomsNear offset drift");
static_assert(offsetof(D2DrlgRoomStrc, pRoom) == 0x30, "D2DrlgRoomStrc::pRoom offset drift");
static_assert(offsetof(D2DrlgRoomStrc, nTileXPos) == 0x34, "D2DrlgRoomStrc::nTileXPos offset drift");
static_assert(offsetof(D2DrlgRoomStrc, nTileYPos) == 0x38, "D2DrlgRoomStrc::nTileYPos offset drift");
static_assert(offsetof(D2DrlgRoomStrc, nTileWidth) == 0x3C, "D2DrlgRoomStrc::nTileWidth offset drift");
static_assert(offsetof(D2DrlgRoomStrc, nTileHeight) == 0x40, "D2DrlgRoomStrc::nTileHeight offset drift");
static_assert(offsetof(D2DrlgRoomStrc, nType) == 0x48, "D2DrlgRoomStrc::nType offset drift");
static_assert(offsetof(D2DrlgRoomStrc, pRoomTiles) == 0x4C, "D2DrlgRoomStrc::pRoomTiles offset drift");
static_assert(offsetof(D2DrlgRoomStrc, pLevel) == 0x58, "D2DrlgRoomStrc::pLevel offset drift");
static_assert(offsetof(D2DrlgRoomStrc, pPresetUnits) == 0x5C, "D2DrlgRoomStrc::pPresetUnits offset drift");

}  // namespace d2bs::imports::extras
