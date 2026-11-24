#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// One row of the launcher's character-select list (1.14d D2Launch, list head at
// 0x779DBC). Not modelled in D2MOO. Single-player and realm lists share it; a
// realm list can end with a "more characters" row and the classic screen adds a
// "create new" row, both told apart by nKind.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::lod114d::imports::extras {

// CharSelEntry::nKind: a character's row. 1 is the "create new" row, 2 the "more characters" row.
constexpr uint32_t CHARSEL_ROW_CHARACTER = 0;

// Bits of CharSelEntry::wFlags: the character's statstring flags, in single player the .d2s status byte (the
// progression is in the high byte).
constexpr uint16_t CHARSEL_FLAG_HARDCORE = 0x04;
constexpr uint16_t CHARSEL_FLAG_DIED = 0x08;
constexpr uint16_t CHARSEL_FLAG_EXPANSION = 0x20;

struct CharSelEntry {
    std::array<char, 0x100> szName;       // 0x000 in the ANSI code page
    std::array<wchar_t, 0x100> wszRealm;  // 0x100 empty for local characters
    std::array<uint8_t, 0x10> nGraphics;  // 0x300
    std::array<uint8_t, 0x10> nColors;    // 0x310
    uint8_t nClass;                       // 0x320 0-based class id
    uint8_t _1;                           // 0x321
    uint16_t wLevel;                      // 0x322
    uint16_t wFlags;                      // 0x324 CHARSEL_FLAG_*
    std::array<uint8_t, 8> _2;            // 0x326
    uint8_t bLadder;                      // 0x32E
    std::array<uint8_t, 5> _3;            // 0x32F
    uint32_t nKind;                       // 0x334 CHARSEL_ROW_*
    void* pCell;                          // 0x338
    uint32_t _4;                          // 0x33C
    uint32_t dwSortKeyLo;                 // 0x340 single player: the save's last-write FILETIME
    uint32_t dwSortKeyHi;                 // 0x344
    uint32_t dwExpires;                   // 0x348 realm expiry (time_t), 0 for local characters
    CharSelEntry* pNext;                  // 0x34C
};

static_assert(sizeof(CharSelEntry) == 0x350, "the game allocates 0x350 bytes per character-select row");
static_assert(offsetof(CharSelEntry, wLevel) == 0x322);
static_assert(offsetof(CharSelEntry, bLadder) == 0x32E);
static_assert(offsetof(CharSelEntry, nKind) == 0x334);
static_assert(offsetof(CharSelEntry, dwExpires) == 0x348);
static_assert(offsetof(CharSelEntry, pNext) == 0x34C);

}  // namespace d2bs::lod114d::imports::extras
// NOLINTEND(readability-identifier-naming)
