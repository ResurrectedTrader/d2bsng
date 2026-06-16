#pragma once

#include <array>
#include <cstdint>

// Battle.net account state - d2bs-internal struct. D2MOO does not model BNCS.
// Field layout reverse-engineered against 1.14d; size verified by static_assert.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::imports::extras {

#pragma pack(push, 1)

struct BnetData {
    uint32_t dwId;                         // 0x00
    uint32_t dwId2;                        // 0x04
    std::array<uint8_t, 0x10> _1;          // 0x08
    uint32_t dwId3;                        // 0x18
    uint16_t wUnk3;                        // 0x1C
    uint8_t _2;                            // 0x1E
    std::array<char, 0x16> szGameName;     // 0x1F
    uint16_t _3;                           // 0x35
    std::array<char, 0x10> szGameIP;       // 0x37
    std::array<uint8_t, 0x42> _5;          // 0x47
    uint32_t dwId4;                        // 0x89
    std::array<char, 0x30> szAccountName;  // 0x8D
    std::array<char, 0x18> szPlayerName;   // 0xBD
    std::array<char, 0x08> szRealmName;    // 0xD5
    std::array<uint8_t, 0x111> _8;         // 0xDD
    uint8_t nCharClass;                    // 0x1EE
    uint8_t nCharFlags;                    // 0x1EF
    uint8_t nMaxDiff;                      // 0x1F0
    std::array<uint8_t, 0x1F> _9;          // 0x1F1
    uint8_t nCreatedGameDifficulty;        // 0x210
    void* _10;                             // 0x211
    std::array<uint8_t, 0x15> _11;         // 0x215
    uint16_t _12;                          // 0x22A
    uint8_t _13;                           // 0x22C
    std::array<char, 0x18> szRealmName2;   // 0x22D
    std::array<char, 0x18> szGamePass;     // 0x245
    std::array<char, 0x102> szGameDesc;    // 0x25D
    std::array<char, 0x20> szChannelName;  // 0x35F
    std::array<uint8_t, 0x40> _14;         // 0x37F
    uint8_t nCharLevel;                    // 0x3BF
    uint8_t nLadderFlag;                   // 0x3C0
    uint32_t dwPassHash;                   // 0x3C1
    uint8_t nPassLength;                   // 0x3C5
};

static_assert(sizeof(BnetData) == 0x3C6, "BnetData must be 0x3C6 bytes");

#pragma pack(pop)

}  // namespace d2bs::imports::extras

namespace d2bs::game {
using ::d2bs::imports::extras::BnetData;
}  // namespace d2bs::game
// NOLINTEND(readability-identifier-naming)
