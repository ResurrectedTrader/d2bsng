#pragma once

#include <array>
#include <cstdint>

// In-game session info - d2bs-internal struct. D2MOO does not model client
// connection state. Field layout reverse-engineered against 1.14d.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::imports::extras {

#pragma pack(push, 1)

struct GameStructInfo {
    std::array<uint8_t, 0x1F> _1;           // 0x000
    std::array<char, 0x18> szGameName;      // 0x01F
    std::array<char, 0x56> szGameServerIp;  // 0x037
    std::array<char, 0x30> szAccountName;   // 0x08D
    std::array<char, 0x18> szCharName;      // 0x0BD
    std::array<char, 0x18> szRealmName;     // 0x0D5
    std::array<uint8_t, 0x158> _2;          // 0x0ED
    std::array<char, 0x18> szGamePassword;  // 0x245
};

static_assert(sizeof(GameStructInfo) == 0x25D, "GameStructInfo must be 0x25D bytes");

#pragma pack(pop)

}  // namespace d2bs::imports::extras

namespace d2bs::game {
using ::d2bs::imports::extras::GameStructInfo;
}  // namespace d2bs::game
// NOLINTEND(readability-identifier-naming)
