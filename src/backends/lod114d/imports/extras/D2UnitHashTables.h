#pragma once

#include <array>
#include <cstdint>

#include <Units/Units.h>  // D2UnitStrc

// Each of the client's two registries has six fixed-type hash tables,
// each 128 buckets indexed by dwUnitId&0x7F. API enumeration uses the
// replicated registry for Player/Monster/Object/Item/Tile and client-only
// for missiles. These are not the D2Game server's registry. Layout matches
// reference/d2bs/D2Structs.h UnitHashTable.
//
// Bucket chain link: each unit's `pListNext` points to the next unit in
// the same bucket (or null at the end). Walking ALL units of a given
// type therefore means: per-bucket, follow the pListNext chain to its
// terminator, then advance to the next non-empty bucket.
namespace d2bs::lod114d::imports::extras {

constexpr uint32_t UNIT_HASH_TYPE_COUNT = 6;
constexpr uint32_t UNIT_HASH_BUCKETS = 128;

#pragma pack(push, 1)

struct D2UnitHashTable {
    std::array<D2UnitStrc*, UNIT_HASH_BUCKETS> buckets;
};

static_assert(sizeof(D2UnitHashTable) == UNIT_HASH_BUCKETS * sizeof(void*));

struct D2UnitHashTables {
    std::array<D2UnitHashTable, UNIT_HASH_TYPE_COUNT> tables;
};

static_assert(sizeof(D2UnitHashTables) == UNIT_HASH_TYPE_COUNT * UNIT_HASH_BUCKETS * sizeof(void*));

#pragma pack(pop)

}  // namespace d2bs::lod114d::imports::extras
