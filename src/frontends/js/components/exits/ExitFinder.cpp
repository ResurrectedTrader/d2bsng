// Framework-side implementation of Level::GetExits.
//
// Game-impl exposes the primitives this needs (room iteration via
// GetFirstRoom + GetNext, cross-level neighbours via GetNearby, preset
// units with tile-target-level lookup, room bounds, collision grids); the
// algorithm itself is purely topological and game-version-agnostic. New
// ports inherit exit-finding by implementing the primitives.
//
// Two passes mirror reference d2bs's ActMap:
//
//   1. Tile pass - walks every UNIT_TILE preset in every room; for each
//      preset whose Room2::pRoomTiles warp table names a destination
//      level, emit an exit at the preset's game-coord position. The
//      destination level lookup runs game-side inside Room::GetPresetUnits
//      and arrives here as PresetUnitInfo::tileTargetLevelId.
//
//   2. Linkage pass - walks every room-pair where a cross-level neighbour
//      sits adjacent across a shared edge. For each pair: derive the edge
//      direction from rectangle overlap, walk every cell along the edge
//      using a 4-probe "is the doorway wide enough?" walkability test
//      (EdgeIsWalkable), record runs of walkable cells, and emit one exit
//      per destination level - the run whose midpoint is closest to the
//      edge centre wins. Reference: ActMap::FindRoomLinkageExits.
//
// Cell sampling reuses the pathfinder's CollisionLookup: secondary level
// grids are lazy-built via BuildLevelGrid on first cross-level cell miss,
// so cells in neighbour levels resolve without any per-call setup. The
// pathfinder and exit finder share one act-wide collision view.

#include "game/Level.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "components/pathfinding/Pathfinder.h"
#include "game/GameLock.h"
#include "game/Room.h"
#include "game/Types.h"

namespace d2bs::game {

namespace {

using pathfinding::BuildLevelGrid;
using pathfinding::CollisionLookup;

constexpr uint32_t UNIT_TILE = 5;

// Reference ActMap::EdgeIsWalkable (ActMap.cpp:438-462): probes four cells
// on the orthogonal axis through the edge point - two in the local room
// (offsets 0 and -1) and two in the adjacent room (offsets +1 and +2).
// All four must be walkable for the edge cell to count.
//
// "Walkable" is the 5-cell cross OR'd against (BLOCK_WALK | BLOCK_PLAYER),
// which is exactly what CollisionLookup::IsBlocked tests - calling it
// with the negation gives us reference's SpaceIsWalkableForExit semantics
// for free.
[[nodiscard]] bool EdgeIsWalkable(CollisionLookup& lookup, Point edge, Point orth) {
    constexpr std::array OFFSETS = {0, -1, 1, 2};
    for (const auto step : OFFSETS) {
        const Point probe{.x = edge.x + (orth.x * step), .y = edge.y + (orth.y * step)};
        if (lookup.IsBlocked(probe)) {
            return false;
        }
    }
    return true;
}

// Reference ActMap::GetEdgeCenterPoint (ActMap.cpp:404-436): walk outward
// from `cur` along the edge axis, tracking the leftmost / rightmost
// walkable cells. Returns the midpoint, used to score a walkable run's
// quality (closer to centre = better).
//
// Bounded by the local level's rect so the walk doesn't wander into
// arbitrary territory - the rectangle comes from `Level::Bounds()` which
// already returns game-coords.
[[nodiscard]] Point GetEdgeCenterPoint(CollisionLookup& lookup, Rect levelBounds, Point cur, Point edgeDir) {
    auto walkable = [&](Point p) {
        return !lookup.IsBlocked(p);
    };

    Point left = cur;
    Point right = cur;

    Point s = cur;
    int32_t i = -1;
    while (levelBounds.Contains(s)) {
        if (walkable(s)) {
            left = s;
        }
        s = {.x = cur.x + (edgeDir.x * i), .y = cur.y + (edgeDir.y * i)};
        --i;
    }
    s = cur;
    int32_t k = 1;
    while (levelBounds.Contains(s)) {
        if (walkable(s)) {
            right = s;
        }
        s = {.x = cur.x + (edgeDir.x * k), .y = cur.y + (edgeDir.y * k)};
        ++k;
    }
    return {.x = (left.x + right.x) / 2, .y = (left.y + right.y) / 2};
}

[[nodiscard]] bool HasExitAt(const std::vector<ExitInfo>& exits, Position pos) {
    return std::ranges::any_of(exits, [&pos](const ExitInfo& e) { return e.pos == pos; });
}

[[nodiscard]] bool HasExitToLevel(const std::vector<ExitInfo>& exits, uint32_t target) {
    return std::ranges::any_of(exits, [target](const ExitInfo& e) { return e.target == target; });
}

}  // namespace

std::vector<ExitInfo> Level::GetExits() const {
    if (!*this) {
        return {};
    }
    GameReadLock guard;
    const uint32_t levelId = Id();
    std::vector<ExitInfo> exits;

    // -----------------------------------------------------------------
    // Pass 1: tile exits - UNIT_TILE presets with a non-zero
    // tileTargetLevelId (resolved game-side via pRoomTiles).
    // -----------------------------------------------------------------
    for (auto room = GetFirstRoom(); room; room = room.GetNext()) {
        for (const auto& preset : room.GetPresetUnits(UNIT_TILE)) {
            if (preset.tileTargetLevelId == 0) {
                continue;
            }
            const auto roomOrigin = room.Bounds().origin;
            const Position pos{.x = roomOrigin.x + preset.posInRoom.x, .y = roomOrigin.y + preset.posInRoom.y};
            if (HasExitAt(exits, pos)) {
                continue;
            }
            exits.push_back(ExitInfo{
                .pos = pos,
                .target = preset.tileTargetLevelId,
                .type = ExitType::Tile,
                .tileId = preset.id,
                .level = levelId,
            });
        }
    }

    // -----------------------------------------------------------------
    // Pass 2: linkage exits - room-pairs across level boundaries.
    //
    // Build a CollisionLookup primed with our level's slab. Cross-level
    // probes lazy-build secondary slabs the first time they miss, so
    // cells in adjacent levels resolve without per-pair setup. Same
    // collision view the pathfinder uses.
    // -----------------------------------------------------------------
    CollisionLookup lookup;
    lookup.primary = BuildLevelGrid(*this);
    const Rect myBounds = Bounds();

    // Collect candidate walkable runs first, keyed by destination level.
    // After collecting we pick the run with the smallest centre-distance
    // per destination, matching reference's "best run per target" rule.
    struct RunCandidate {
        Position pos;
        int64_t centreDistSq;
    };
    std::multimap<uint32_t, RunCandidate> candidates;

    for (auto room = GetFirstRoom(); room; room = room.GetNext()) {
        const Rect a = room.Bounds();
        const int32_t aMinX = static_cast<int32_t>(a.origin.x);
        const int32_t aMinY = static_cast<int32_t>(a.origin.y);
        const int32_t aMaxX = aMinX + static_cast<int32_t>(a.size.width);
        const int32_t aMaxY = aMinY + static_cast<int32_t>(a.size.height);

        for (auto neighbour : room.GetNearby()) {
            const uint32_t neighbourLevel = neighbour.LevelId();
            if (neighbourLevel == levelId) {
                continue;
            }
            if (HasExitToLevel(exits, neighbourLevel)) {
                continue;
            }

            const Rect b = neighbour.Bounds();
            const int32_t bMinX = static_cast<int32_t>(b.origin.x);
            const int32_t bMinY = static_cast<int32_t>(b.origin.y);
            const int32_t bMaxX = bMinX + static_cast<int32_t>(b.size.width);
            const int32_t bMaxY = bMinY + static_cast<int32_t>(b.size.height);

            const int32_t overlapX = std::min(aMaxX, bMaxX) - std::max(aMinX, bMinX);
            const int32_t overlapY = std::min(aMaxY, bMaxY) - std::max(aMinY, bMinY);

            // Reference rejects (ActMap.cpp:325-336): corner-only contact
            // (one overlap negative), area overlap (both positive), or a
            // shared edge thinner than 3 game-coords (no realistic doorway).
            if (overlapX < 0 || overlapY < 0) {
                continue;
            }
            if (overlapX > 0 && overlapY > 0) {
                continue;
            }
            if (overlapX < 3 && overlapY < 3) {
                continue;
            }

            // Pick start corner, edge axis, and orthogonal direction
            // (pointing away from local room into neighbour).
            int32_t startX = 0;
            int32_t startY = 0;
            int32_t edgeDirX = 0;
            int32_t edgeDirY = 0;
            int32_t orthX = 0;
            int32_t orthY = 0;
            int32_t edgeSize = 0;
            if (overlapX > 0) {
                edgeDirX = 1;
                edgeSize = overlapX;
                const int32_t baseX = std::max(aMinX, bMinX);
                if (aMinY < bMinY) {
                    startX = baseX;
                    startY = aMaxY - 1;
                    orthY = 1;
                } else {
                    startX = baseX;
                    startY = aMinY;
                    orthY = -1;
                }
            } else {
                edgeDirY = 1;
                edgeSize = overlapY;
                const int32_t baseY = std::max(aMinY, bMinY);
                if (aMinX < bMinX) {
                    startX = aMaxX - 1;
                    startY = baseY;
                    orthX = 1;
                } else {
                    startX = aMinX;
                    startY = baseY;
                    orthX = -1;
                }
            }

            // Walk every cell along the shared edge with the 4-probe
            // EdgeIsWalkable check.
            std::vector walkable(static_cast<size_t>(edgeSize), false);
            for (int32_t j = 0; j < edgeSize; ++j) {
                const Point edgeCell{.x = startX + (edgeDirX * j), .y = startY + (edgeDirY * j)};
                const Point orth{.x = orthX, .y = orthY};
                walkable[static_cast<size_t>(j)] = EdgeIsWalkable(lookup, edgeCell, orth);
            }

            // Scan for runs. At each run-end (non-walkable cell or end of
            // edge), score the run by distance from edge centre and emit
            // a candidate. The best per destination is picked after the
            // loop.
            int32_t lastWalkX = 0;
            int32_t lastWalkY = 0;
            int32_t spaces = 0;
            for (int32_t j = 0; j < edgeSize; ++j) {
                const bool isWalk = walkable[static_cast<size_t>(j)];
                const int32_t curX = startX + (edgeDirX * j);
                const int32_t curY = startY + (edgeDirY * j);
                if (isWalk) {
                    lastWalkX = curX;
                    lastWalkY = curY;
                    spaces++;
                }
                if (!isWalk || j + 1 == edgeSize) {
                    if (spaces > 0) {
                        const Point cur{.x = curX, .y = curY};
                        const Point edgeDir{.x = edgeDirX, .y = edgeDirY};
                        const auto centre = GetEdgeCenterPoint(lookup, myBounds, cur, edgeDir);
                        const int32_t halfSpaces = spaces / 2;
                        const int32_t runMidX = lastWalkX - (edgeDirX * halfSpaces);
                        const int32_t runMidY = lastWalkY - (edgeDirY * halfSpaces);
                        const int64_t dx = runMidX - centre.x;
                        const int64_t dy = runMidY - centre.y;
                        candidates.emplace(neighbourLevel,
                                           RunCandidate{
                                               .pos = {.x = static_cast<uint32_t>(std::max(runMidX, 0)),
                                                       .y = static_cast<uint32_t>(std::max(runMidY, 0))},
                                               .centreDistSq = (dx * dx) + (dy * dy),
                                           });
                    }
                    spaces = 0;
                }
            }
        }
    }

    // Pick the best (smallest centre-distance) run for each destination
    // level and emit as the linkage exit.
    for (auto it = candidates.begin(); it != candidates.end();) {
        const uint32_t target = it->first;
        auto upper = candidates.upper_bound(target);
        const RunCandidate* best = &it->second;
        for (auto cur = std::next(it); cur != upper; ++cur) {
            if (cur->second.centreDistSq < best->centreDistSq) {
                best = &cur->second;
            }
        }
        if (!HasExitToLevel(exits, target)) {
            exits.push_back(ExitInfo{
                .pos = best->pos,
                .target = target,
                .type = ExitType::Linkage,
                .tileId = 0,
                .level = levelId,
            });
        }
        it = upper;
    }

    return exits;
}

}  // namespace d2bs::game
