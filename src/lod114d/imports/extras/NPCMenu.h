#pragma once

#include <array>
#include <cstdint>

// NPC dialog menu table - d2bs-internal struct. Not modelled in D2MOO.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::imports::extras {

#pragma pack(push, 1)

// Entry function - the menu handler the game invokes when the user picks
// a row. Takes no args, returns nothing; calling-convention is irrelevant
// in practice (with zero parameters, cdecl/stdcall/fastcall emit the same
// call sequence).
struct NPCMenu {
    uint32_t dwNPCClassId;
    uint32_t dwEntryAmount;
    uint16_t wEntryId1;
    uint16_t wEntryId2;
    uint16_t wEntryId3;
    uint16_t wEntryId4;
    uint16_t _1;  // WORD padding
    void (*pEntryFunc1)();
    void (*pEntryFunc2)();
    void (*pEntryFunc3)();
    void (*pEntryFunc4)();
    std::array<uint8_t, 5> _2;  // trailing padding
};

static_assert(sizeof(NPCMenu) == 0x27, "NPCMenu must be 0x27 bytes");

#pragma pack(pop)

}  // namespace d2bs::imports::extras

namespace d2bs::game {
using ::d2bs::imports::extras::NPCMenu;
}  // namespace d2bs::game
// NOLINTEND(readability-identifier-naming)
