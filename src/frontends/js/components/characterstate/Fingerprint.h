#pragma once

#include <cstddef>
#include <vector>

#include "game/Types.h"
#include "game/Unit.h"

namespace d2bs::js::characterstate {

// Change-detection fingerprints. These run every tick to decide whether anything moved, so
// they stream the field walk (VisitUnit through a hashing sink) rather than building a json
// document and hashing its dump - the walk reads the same game-memory fields either way, but
// the json path allocated a tree and a string per tick, which dominated the game-thread cost
// (see docs/character-capture-fingerprint-cost.md).

// A container's fingerprint: its declared grid plus every item's structural fields. The
// grid is part of it because the belt's is not fixed - swapping a sash for a girdle grows
// it while the potions stay put, so a contents-only hash would leave the manager decomposing
// slots against a stale grid.
size_t ContainerHash(const std::vector<game::Unit>& items, game::Size dims);

// A wearer's fingerprint (identity, skills, and for the player area + weapon set). Its
// volatile merged stats are fingerprinted separately by the caller.
size_t UnitHash(const game::Unit& unit);

}  // namespace d2bs::js::characterstate
