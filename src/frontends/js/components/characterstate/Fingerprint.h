#pragma once

#include <cstddef>
#include <vector>

#include "game/Types.h"
#include "game/Unit.h"

namespace d2bs::js::characterstate {

// Per-tick change-detection hashes: VisitUnit through a hashing sink, allocation-free
// because this runs every tick (see docs/character-capture-fingerprint-cost.md).

// The grid is part of the hash because the belt's is not fixed - swap a sash for a girdle
// and it grows while the potions stay put, so a contents-only hash would leave the manager
// decomposing slots against a stale grid.
size_t ContainerHash(const std::vector<game::Unit>& items, game::Size dims);

size_t UnitHash(const game::Unit& unit);

// A stable "absent unit" sentinel, distinct from any real unit's hash (which always covers
// at least unitType + classId). Used for the no-merc case.
size_t EmptyUnitHash();

}  // namespace d2bs::js::characterstate
