#pragma once

#include <array>
#include <cstdint>

// Transaction dialog UI structs - d2bs-internal. Not modelled in D2MOO.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::imports::extras {

struct TransactionDialogsLine {
    std::array<wchar_t, 120> wszText;  // 0x000
    std::array<uint32_t, 6> dwUnk;     // 0x0F0
    void(__stdcall* pfHandler)();      // 0x108
    uint32_t bIsSelectable;            // 0x10C
};

static_assert(sizeof(TransactionDialogsLine) == 0x110, "TransactionDialogsLine must be 0x110 bytes");

struct TransactionDialogsInfo {
    std::array<uint32_t, 0x14> dwUnk1;                    // 0x000
    uint32_t dwNumLines;                                  // 0x050
    std::array<uint32_t, 0x5> dwUnk2;                     // 0x054
    std::array<TransactionDialogsLine, 10> aDialogLines;  // 0x068
    void* pSomething;                                     // 0xB08
};

static_assert(sizeof(TransactionDialogsInfo) == 0xB0C, "TransactionDialogsInfo must be 0xB0C bytes");

}  // namespace d2bs::imports::extras

namespace d2bs::game {
using ::d2bs::imports::extras::TransactionDialogsInfo;
using ::d2bs::imports::extras::TransactionDialogsLine;
}  // namespace d2bs::game
// NOLINTEND(readability-identifier-naming)
