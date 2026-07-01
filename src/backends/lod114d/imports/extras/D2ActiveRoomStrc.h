#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2ActiveRoomStrc` has different offsets
// for 1.14d (D2MOO was reverse-engineered against 1.10c). Reference d2bs's
// CODE reads these fields at the bytes pinned here, and reference works on
// 1.14d. Use this struct via `d2bs::imports::extras::D2ActiveRoomStrc` or
// via `using d2bs::imports::extras::D2ActiveRoomStrc` to shadow D2MOO's
// version inside the consuming TU.
//
// D2MOO's claimed size is 0x80, which happens to match the 1.14d allocation
// size - but every named field except for `pUnitFirst`'s offset disagrees.
// Reference Room1 has pRoomsNear @ 0x00, pRoom2 @ 0x10, Coll @ 0x20, and
// pUnitFirst @ 0x74; D2MOO front-loads tCoords at 0x00 and puts pUnitFirst
// at 0x2C. We model the 1.14d allocation here.
//
// Field naming follows D2MOO (`ppRoomList`, `pDrlgRoom`, `pCollisionGrid`,
// `nNumRooms`, `pUnitFirst`, `pRoomNext`); offsets follow reference.

struct D2RoomCollisionGridStrc;
struct D2UnitStrc;

namespace d2bs::imports::extras {

struct D2DrlgRoomStrc;
struct D2ActiveRoomStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2ActiveRoomStrc {
    D2ActiveRoomStrc** ppRoomList;            // 0x00 - reference Room1::pRoomsNear
    std::array<uint32_t, 3> _1;               // 0x04 - opaque (reference Room1._1[3])
    D2DrlgRoomStrc* pDrlgRoom;                // 0x10 - reference Room1::pRoom2
    std::array<uint32_t, 3> _2;               // 0x14 - opaque (reference Room1._2[3])
    D2RoomCollisionGridStrc* pCollisionGrid;  // 0x20 - reference Room1::Coll (CollMap*)
    int32_t nNumRooms;                        // 0x24 - reference Room1::dwRoomsNear
    std::array<uint32_t, 9> _3;               // 0x28 - opaque (reference Room1._3[9])
    int32_t dwXStart;                         // 0x4C - reference Room1::dwXStart
    int32_t dwYStart;                         // 0x50 - reference Room1::dwYStart
    int32_t dwXSize;                          // 0x54 - reference Room1::dwXSize
    int32_t dwYSize;                          // 0x58 - reference Room1::dwYSize
    std::array<uint32_t, 6> _4;               // 0x5C - opaque (reference Room1._4[6])
    D2UnitStrc* pUnitFirst;                   // 0x74 - reference Room1::pUnitFirst
    uint32_t _5;                              // 0x78 - opaque (reference Room1._5)
    D2ActiveRoomStrc* pRoomNext;              // 0x7C - reference Room1::pRoomNext
};
// NOLINTEND(readability-identifier-naming)

static_assert(sizeof(D2ActiveRoomStrc) == 0x80, "D2ActiveRoomStrc must be 0x80 bytes (1.14d)");
static_assert(offsetof(D2ActiveRoomStrc, ppRoomList) == 0x00, "D2ActiveRoomStrc::ppRoomList offset drift");
static_assert(offsetof(D2ActiveRoomStrc, pDrlgRoom) == 0x10, "D2ActiveRoomStrc::pDrlgRoom offset drift");
static_assert(offsetof(D2ActiveRoomStrc, pCollisionGrid) == 0x20, "D2ActiveRoomStrc::pCollisionGrid offset drift");
static_assert(offsetof(D2ActiveRoomStrc, nNumRooms) == 0x24, "D2ActiveRoomStrc::nNumRooms offset drift");
static_assert(offsetof(D2ActiveRoomStrc, dwXStart) == 0x4C, "D2ActiveRoomStrc::dwXStart offset drift");
static_assert(offsetof(D2ActiveRoomStrc, dwYStart) == 0x50, "D2ActiveRoomStrc::dwYStart offset drift");
static_assert(offsetof(D2ActiveRoomStrc, dwXSize) == 0x54, "D2ActiveRoomStrc::dwXSize offset drift");
static_assert(offsetof(D2ActiveRoomStrc, dwYSize) == 0x58, "D2ActiveRoomStrc::dwYSize offset drift");
static_assert(offsetof(D2ActiveRoomStrc, pUnitFirst) == 0x74, "D2ActiveRoomStrc::pUnitFirst offset drift");
static_assert(offsetof(D2ActiveRoomStrc, pRoomNext) == 0x7C, "D2ActiveRoomStrc::pRoomNext offset drift");

}  // namespace d2bs::imports::extras
