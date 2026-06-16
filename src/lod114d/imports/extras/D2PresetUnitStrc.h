#pragma once

#include <cstddef>
#include <cstdint>

// 1.14d-correct layout. D2MOO's `::D2PresetUnitStrc` has different offsets
// for 1.14d (D2MOO was reverse-engineered against 1.10c). Reference d2bs's
// CODE reads these fields at the bytes pinned here, and reference works on
// 1.14d. Use this struct via `d2bs::imports::extras::D2PresetUnitStrc` or
// via `using d2bs::imports::extras::D2PresetUnitStrc` to shadow D2MOO's
// version inside the consuming TU.
//
// D2MOO's claimed size is 0x20 with nUnitType @ 0x00 / nIndex @ 0x04 /
// nMode @ 0x08 / nXpos @ 0x0C / nYpos @ 0x10 / bSpawned @ 0x14 /
// pMapAI @ 0x18 / pNext @ 0x1C. Reference PresetUnit is 0x1C with the
// type-and-position fields shuffled: dwTxtFileNo @ 0x04, dwPosX @ 0x08,
// pPresetNext @ 0x0C, dwType @ 0x14, dwPosY @ 0x18. Only nIndex /
// dwTxtFileNo at 0x04 share a byte; everything else needs the
// reference layout.
//
// Field naming follows D2MOO (`nIndex`, `nXpos`, `nYpos`, `nUnitType`,
// `pNext`); offsets follow reference. Note that `nUnitType` lands at
// 0x14 (reference dwType), not at 0x00 - this is the most surprising
// deviation, since both naming conventions agree on the field's
// purpose but disagree on its placement.

namespace d2bs::imports::extras {

struct D2PresetUnitStrc;

// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
struct D2PresetUnitStrc {
    uint32_t _1;              // 0x00 - opaque (reference PresetUnit._1)
    int32_t nIndex;           // 0x04 - reference PresetUnit::dwTxtFileNo
    int32_t nXpos;            // 0x08 - reference PresetUnit::dwPosX
    D2PresetUnitStrc* pNext;  // 0x0C - reference PresetUnit::pPresetNext
    uint32_t _3;              // 0x10 - opaque (reference PresetUnit._3)
    int32_t nUnitType;        // 0x14 - reference PresetUnit::dwType
    int32_t nYpos;            // 0x18 - reference PresetUnit::dwPosY
};
// NOLINTEND(readability-identifier-naming)

static_assert(sizeof(D2PresetUnitStrc) == 0x1C, "D2PresetUnitStrc must be 0x1C bytes (1.14d)");
static_assert(offsetof(D2PresetUnitStrc, nIndex) == 0x04, "D2PresetUnitStrc::nIndex offset drift");
static_assert(offsetof(D2PresetUnitStrc, nXpos) == 0x08, "D2PresetUnitStrc::nXpos offset drift");
static_assert(offsetof(D2PresetUnitStrc, pNext) == 0x0C, "D2PresetUnitStrc::pNext offset drift");
static_assert(offsetof(D2PresetUnitStrc, nUnitType) == 0x14, "D2PresetUnitStrc::nUnitType offset drift");
static_assert(offsetof(D2PresetUnitStrc, nYpos) == 0x18, "D2PresetUnitStrc::nYpos offset drift");

}  // namespace d2bs::imports::extras
