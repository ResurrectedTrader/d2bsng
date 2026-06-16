#pragma once

// TestActMap: a test-only ActMap replacement backed by our LevelGrid.
// Implements the GetMapData / SpaceGetData / CollisionFlag interface
// that the reference pathfinding code expects, reading from a flat grid
// instead of live game memory.

#include "Types.h"
#include "components/pathfinding/Pathfinder.h"

class TestActMap {
    std::vector<const d2bs::pathfinding::LevelGrid*> grids_;

    const d2bs::pathfinding::LevelGrid* FindGrid(int32_t x, int32_t y) const {
        if (x < 0 || y < 0)
            return nullptr;
        ::d2bs::game::Position pos{.x = static_cast<uint32_t>(x), .y = static_cast<uint32_t>(y)};
        for (auto* g : grids_) {
            if (g->Contains(pos))
                return g;
        }
        return nullptr;
    }

   public:
    enum CollisionFlag {
        None = 0x0000,
        BlockWalk = 0x0001,
        BlockLineOfSight = 0x0002,
        Wall = 0x0004,
        BlockPlayer = 0x0008,
        AlternateTile = 0x0010,
        Blank = 0x0020,
        Missile = 0x0040,
        Player = 0x0080,
        NPCLocation = 0x0100,
        Item = 0x0200,
        Object = 0x0400,
        ClosedDoor = 0x0800,
        NPCCollision = 0x1000,
        FriendlyNPC = 0x2000,
        Unknown = 0x4000,
        DeadBody = 0x8000,
        Avoid = 0xffff,
    };

    // Single grid (backward compatible)
    explicit TestActMap(const d2bs::pathfinding::LevelGrid* grid) : grids_{grid} {}

    // Multiple grids (cross-level)
    explicit TestActMap(std::vector<const d2bs::pathfinding::LevelGrid*> grids) : grids_(std::move(grids)) {}

    int GetMapData(const Point& point) {
        auto* g = FindGrid(point.x, point.y);
        if (!g)
            return Avoid;
        return g->Get({.x = static_cast<uint32_t>(point.x), .y = static_cast<uint32_t>(point.y)});
    }

    int SpaceGetData(const Point& point, int32_t radius = 1) {
        int val = GetMapData(point);
        val |= GetMapData({.x = point.x - radius, .y = point.y});
        val |= GetMapData({.x = point.x + radius, .y = point.y});
        val |= GetMapData({.x = point.x, .y = point.y - radius});
        val |= GetMapData({.x = point.x, .y = point.y + radius});
        return val;
    }

    void maybe_release_lock() {}
};

// Make TestActMap available as "ActMap" so reference code that uses
// the unqualified name (e.g. checkFlag referencing ActMap::BlockWalk) compiles.
using ActMap = TestActMap;
