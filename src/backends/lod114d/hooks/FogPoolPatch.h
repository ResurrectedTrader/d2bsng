#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace d2bs::lod114d::hooks {
struct FogPoolLookupPatch {
    std::array<uint8_t, 25> original;
    std::array<uint8_t, 25> replacement;
};

// The absolute IAT operand has an IMAGE_REL_BASED_HIGHLOW relocation in Game.exe.
// Validate and emit the loaded address, including when a host maps the game away from 0x400000.
constexpr FogPoolLookupPatch BuildFogPoolLookupPatch(uint32_t moduleBase) {
    static_assert(std::endian::native == std::endian::little);
    FogPoolLookupPatch patch{
        .original = {0x8B, 0xBE, 0xA8, 0x07, 0, 0,      // mov edi, [esi+7A8h]  ; index = system->index (too early)
                     0x8D, 0x5E, 0x04,                  // lea ebx, [esi+4]     ; lock = &system->lock
                     0x53,                              // push ebx             ; EnterCriticalSection argument
                     0x89, 0x5D, 0xFC,                  // mov [ebp-4], ebx     ; save lock for LeaveCriticalSection
                     0xFF, 0x15, 0,    0,    0, 0,      // call [base+2CC218h]       ; EnterCriticalSection(lock)
                     0x8B, 0xB6, 0xA0, 0x07, 0, 0},     // mov esi, [esi+7A0h]  ; count = system->count
        .replacement = {0x8D, 0x5E, 0x04,               // lea ebx, [esi+4]     ; lock = &system->lock
                        0x53,                           // push ebx             ; EnterCriticalSection argument
                        0x89, 0x5D, 0xFC,               // mov [ebp-4], ebx     ; preserve native lock local
                        0xFF, 0x15, 0,    0,    0, 0,   // call [base+2CC218h]       ; EnterCriticalSection(lock)
                        0x8B, 0xBE, 0xA8, 0x07, 0, 0,   // mov edi, [esi+7A8h]  ; index = system->index (now protected)
                        0x8B, 0xB6, 0xA0, 0x07, 0, 0},  // mov esi, [esi+7A0h]  ; count = system->count
    };
    constexpr uint32_t ENTER_CRITICAL_SECTION_IAT_RVA = 0x2CC218;
    const auto operand = std::bit_cast<std::array<uint8_t, 4>>(moduleBase + ENTER_CRITICAL_SECTION_IAT_RVA);
    // The call starts at byte 13 in the original layout, byte 7 after moving the index load.
    std::ranges::copy(operand, patch.original.begin() + 15);
    std::ranges::copy(operand, patch.replacement.begin() + 9);
    return patch;
}
}  // namespace d2bs::lod114d::hooks
