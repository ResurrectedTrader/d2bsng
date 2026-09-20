#pragma once

#include <cstdint>
#include <vector>

#include "game/Level.h"
#include "game/Types.h"

namespace d2bs::navigation {

// `type` is either ExitType::Linkage (room-to-room edge between two
// different levels) or ExitType::Tile (UNIT_TILE preset with a non-zero
// destination via the source room's pRoomTiles warp table). The JS API
// surface treats both identically - the type tag is just metadata.
enum class ExitType : uint32_t {
    Linkage = 1,
    Tile = 2,
};

struct ExitInfo {
    game::Position pos;  // game coordinates - see docs/coords.md
    uint32_t target;
    ExitType type;
    uint32_t tileId;
    uint32_t level;
};

// Level transitions leading out of `level`: tile warps and room-to-room
// edges into an adjacent level. Derived entirely from the contract's room
// iteration and collision primitives, so every backend gets it for free.
std::vector<ExitInfo> GetExits(game::Level level);

}  // namespace d2bs::navigation
