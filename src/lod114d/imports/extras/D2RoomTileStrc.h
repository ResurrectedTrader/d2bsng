#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2RoomTileStrc` has different offsets
// for 1.14d (D2MOO was reverse-engineered against 1.10c). Reference d2bs's
// CODE reads these fields at the bytes pinned here, and reference works on
// 1.14d. Use this struct via `d2bs::imports::extras::D2RoomTileStrc` or
// via `using d2bs::imports::extras::D2RoomTileStrc` to shadow D2MOO's
// version inside the consuming TU.
//
// D2MOO's claimed size is 0x18 with pDrlgRoom @ 0x00 / pLvlWarpTxtRecord @ 0x04 /
// bEnabled @ 0x08 / unk0x0C @ 0x0C / unk0x10 @ 0x10 / pNext @ 0x14.
// Reference RoomTile is 0x14 with pRoom2 @ 0x00 / pNext @ 0x04 /
// _2[2] @ 0x08 / nNum @ 0x10. Reference's `nNum` is read as a DWORD*
// matched against PresetUnit::dwTxtFileNo (preset tile id). D2MOO's
// equivalent at 0x10 is interpreted as a D2DrlgTileDataStrc* with a
// packed-tile bitfield decode at its dwFlags member. The byte itself
// (0x10) holds a pointer in both interpretations - only the downstream
// pointee shape differs. Consumers needing the D2MOO bitfield-decode
// view can reinterpret `pPresetTileId` as `D2DrlgTileDataStrc*`.
//
// Field naming uses D2MOO conventions where possible:
//   - pDrlgRoom      (D2MOO; reference's pRoom2)
//   - pNext          (both reference and D2MOO use this name; reference
//                     places it at 0x04, D2MOO at 0x14 - we pin the
//                     reference offset since 1.14d's tile-list traversal
//                     matches reference)
//   - pPresetTileId  (DWORD*; dereferenced value is the preset tile id
//                     matched against PresetUnit::dwTxtFileNo to resolve
//                     a warp tile's destination level. Reference calls
//                     this `nNum`; D2MOO names the same byte `unk0x10`.)

namespace d2bs::imports::extras {

struct D2DrlgRoomStrc;
struct D2RoomTileStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2RoomTileStrc {
    D2DrlgRoomStrc* pDrlgRoom;   // 0x00 - reference RoomTile::pRoom2
    D2RoomTileStrc* pNext;       // 0x04 - reference RoomTile::pNext
    std::array<uint32_t, 2> _2;  // 0x08 - opaque (reference RoomTile._2[2])
    uint32_t* pPresetTileId;     // 0x10 - reference RoomTile::nNum (DWORD*)
};
// NOLINTEND(readability-identifier-naming)

static_assert(sizeof(D2RoomTileStrc) == 0x14, "D2RoomTileStrc must be 0x14 bytes (1.14d)");
static_assert(offsetof(D2RoomTileStrc, pDrlgRoom) == 0x00, "D2RoomTileStrc::pDrlgRoom offset drift");
static_assert(offsetof(D2RoomTileStrc, pNext) == 0x04, "D2RoomTileStrc::pNext offset drift");
static_assert(offsetof(D2RoomTileStrc, pPresetTileId) == 0x10, "D2RoomTileStrc::pPresetTileId offset drift");

}  // namespace d2bs::imports::extras
