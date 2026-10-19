#pragma once

#include "game/Types.h"
#include "imports/D2Client.h"

#include <cstdint>

namespace d2bs::game::unit_tables {

// Lookup and enumeration share the same registry: client-only missiles,
// replicated units for every other type. IDs can collide across registries.
inline const lod114d::imports::extras::D2UnitHashTable* TableForType(UnitType type) {
    const auto typeIdx = static_cast<uint32_t>(type);
    if (typeIdx >= lod114d::imports::extras::UNIT_HASH_TYPE_COUNT) {
        return nullptr;
    }
    const auto* tables = type == UnitType::Missile ? lod114d::imports::d2client::gClientOnlyUnitHashTables.Ptr()
                                                   : lod114d::imports::d2client::gReplicatedUnitHashTables.Ptr();
    if (tables == nullptr) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by UNIT_HASH_TYPE_COUNT above
    return &tables->tables[typeIdx];
}

// Callers hold the game lock. Matches 1.14d 0x00463940's bucket walk;
// reject a mismatched type instead of the game's fatal assertion.
inline D2UnitStrc* FindUnitInHashTable(uint32_t id, UnitType type) {
    const auto* table = TableForType(type);
    if (table == nullptr) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - id mask bounds the bucket index
    for (auto* unit = table->buckets[id & 0x7FU]; unit != nullptr; unit = unit->pListNext) {
        if (unit->dwUnitId == id) {
            return unit->dwUnitType == static_cast<uint32_t>(type) ? unit : nullptr;
        }
    }
    return nullptr;
}

}  // namespace d2bs::game::unit_tables
