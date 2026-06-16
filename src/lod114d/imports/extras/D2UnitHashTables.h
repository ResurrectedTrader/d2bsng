#pragma once

#include <array>
#include <cstdint>

#include <Units/Units.h>  // D2UnitStrc

// D2's unit registry: 6 fixed-type hash tables, each 128 buckets indexed
// by `dwUnitId & 0x7F`. The server-side table holds 5 of the 6 types
// (Player/Monster/Object/Item/Tile); missiles live in the client-side
// table at type slot 3. Reference parity:
// reference/d2bs/D2Structs.h `UnitHashTable`.
//
// Bucket chain link: each unit's `pListNext` points to the next unit in
// the same bucket (or null at the end). Walking ALL units of a given
// type therefore means: per-bucket, follow the pListNext chain to its
// terminator, then advance to the next non-empty bucket.
namespace d2bs::imports::extras {

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

}  // namespace d2bs::imports::extras

namespace d2bs::game {
using ::d2bs::imports::extras::D2UnitHashTable;
using ::d2bs::imports::extras::D2UnitHashTables;
using ::d2bs::imports::extras::UNIT_HASH_BUCKETS;
using ::d2bs::imports::extras::UNIT_HASH_TYPE_COUNT;
}  // namespace d2bs::game
