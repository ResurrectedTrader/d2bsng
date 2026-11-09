#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

#include "../../../../src/backends/lod114d/hooks/FogPoolPatch.h"

TEST_CASE("Fog lookup repair follows the PE relocation and preserves its instructions") {
    // Original 1.14d bytes: the PE relocation at RVA 0x99E8 covers the call operand (offset 15).
    constexpr std::array<uint8_t, 25> ORIGINAL = {0x8B, 0xBE, 0xA8, 0x07, 0,    0,    0x8D, 0x5E, 0x04,
                                                  0x53, 0x89, 0x5D, 0xFC, 0xFF, 0x15, 0x18, 0xC2, 0x6C,
                                                  0,    0x8B, 0xB6, 0xA0, 0x07, 0,    0};
    // Preferred base, positive relocation, high manual-map address and a negative relocation delta.
    constexpr std::array<uint32_t, 4> BASES = {0x400000, 0x500000, 0x68000000, 0x100000};
    for (const uint32_t base : BASES) {
        CAPTURE(base);
        auto loaded = ORIGINAL;
        uint32_t operand = 0;
        std::memcpy(&operand, loaded.data() + 15, sizeof(operand));
        operand += base - 0x400000U;  // Apply IMAGE_REL_BASED_HIGHLOW as a PE loader does.
        std::memcpy(loaded.data() + 15, &operand, sizeof(operand));
        const auto patch = d2bs::lod114d::hooks::BuildFogPoolLookupPatch(base);
        CHECK(patch.original == loaded);

        // The repair moves only the six-byte index load past the lock-entry instructions.
        std::array<uint8_t, 25> repaired{};
        std::ranges::copy(std::span{loaded}.subspan(6, 13), repaired.begin());
        std::ranges::copy(std::span{loaded}.first(6), repaired.begin() + 13);
        std::ranges::copy(std::span{loaded}.last(6), repaired.begin() + 19);
        CHECK(patch.replacement == repaired);
        uint32_t repairedOperand = 0;
        std::memcpy(&repairedOperand, patch.replacement.data() + 9, sizeof(repairedOperand));
        CHECK(repairedOperand == operand);
    }
}
