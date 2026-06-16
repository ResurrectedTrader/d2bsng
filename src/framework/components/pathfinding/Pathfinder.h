#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <unordered_map>
#include <vector>

#include "game/Types.h"

namespace d2bs::game {
class Level;
}  // namespace d2bs::game

namespace d2bs::pathfinding {

// Geometric primitives are defined in game/Types.h; alias them here so pathfinding
// code and consumers can write `d2bs::pathfinding::{Point,Position,Size,Rect}` directly.
using Point = d2bs::game::Point;
using Position = d2bs::game::Position;
using Size = d2bs::game::Size;
using Rect = d2bs::game::Rect;

enum class ReductionType : int32_t {
    Walk = 0,
    Teleport = 1,
    None = 2,
    JSCallback = 3,
};

namespace collision {
inline constexpr uint16_t BLOCK_WALK = 0x0001;
inline constexpr uint16_t BLOCK_PLAYER = 0x0008;
inline constexpr uint16_t OBJECT = 0x0400;
inline constexpr uint16_t CLOSED_DOOR = 0x0800;
inline constexpr uint16_t AVOID = 0xFFFF;
}  // namespace collision

// Row-major cell index for `pos` within rectangle `r`. Shared by LevelGrid,
// LevelNodes (in Pathfinder.cpp), and the fast-path window helpers below.
// Caller must have verified that `pos` lies within `r`.
[[gnu::always_inline]] constexpr size_t CellIndex(Rect r, Position pos) {
    return (static_cast<size_t>(pos.y - r.origin.y) * r.size.width) + (pos.x - r.origin.x);
}

struct LevelGrid {
    std::vector<uint16_t> data;
    Rect rect;

    // Identity of the grid: which level it represents, and the map seed of
    // the game that produced it. Both default to 0; BuildLevelGrid stamps
    // them. Together they uniquely identify a generated map - same levelId
    // across two games has different room layouts because the map seed (D2
    // randomly generates each level per game) differs. Caches (e.g.
    // FindPath's thread-local) compare both to know if a cached slab is
    // still valid for a follow-up request.
    uint32_t levelId = 0;
    uint32_t mapSeed = 0;

    LevelGrid() = default;
    LevelGrid(Rect r, uint16_t fill = 0) : data(r.size.Area(), fill), rect(r) {}

    [[gnu::always_inline]] bool Contains(Position p) const { return rect.Contains(p); }
    // Unchecked read - caller has verified Contains(p). Used by CollisionLookup
    // hot paths to skip a second Contains() after the primary/secondary probe.
    [[gnu::always_inline]] uint16_t GetUnchecked(Position p) const { return data[CellIndex(rect, p)]; }
    [[gnu::always_inline]] uint16_t Get(Position p) const { return Contains(p) ? GetUnchecked(p) : collision::AVOID; }
    void Set(Position p, uint16_t value);
};

// Pre-validated view into a LevelGrid's cell data. Built by OpenWindow() once
// the caller has confirmed that a margin-radius neighbourhood around a probe
// point fits entirely inside the grid. Cross()/Wide() then perform direct
// reads with no per-cell bounds tests - this is the fast-path backbone.
struct GridWindow {
    std::span<const uint16_t> cells;
    ptrdiff_t stride;  // signed so stride arithmetic with negative offsets needs no casts
    ptrdiff_t base;    // flat index of the window's center cell

    // Hides the signed->unsigned conversion that span::operator[] requires.
    [[gnu::always_inline]] uint16_t At(ptrdiff_t idx) const { return cells[static_cast<size_t>(idx)]; }

    // OR of a 5-cell plus-stencil at radius 1 around (base + dx, base + dy):
    //   . # .
    //   # # #
    //   . # .
    [[gnu::always_inline]] uint16_t Cross(ptrdiff_t dx = 0, ptrdiff_t dy = 0) const {
        ptrdiff_t c = base + (dy * stride) + dx;
        return static_cast<uint16_t>(At(c) | At(c - 1) | At(c + 1) | At(c - stride) | At(c + stride));
    }

    // OR of a 5-cell plus-stencil at radius 2 around `base`:
    //   . . # . .
    //   . . . . .
    //   # . # . #
    //   . . . . .
    //   . . # . .
    [[gnu::always_inline]] uint16_t Wide() const {
        ptrdiff_t s2 = 2 * stride;
        return static_cast<uint16_t>(At(base) | At(base - 2) | At(base + 2) | At(base - s2) | At(base + s2));
    }
};

// Returns a window centered at `p` extending `margin` cells in each direction,
// or nullopt if any cell in that range would lie outside `g`. Callers use it
// to short-circuit a series of probes into one bounds check + direct reads.
[[gnu::always_inline]] inline std::optional<GridWindow> OpenWindow(const LevelGrid& g, Point p, uint32_t margin) {
    // Guard negative coords first so the signed->unsigned cast in ToPosition is safe.
    if (p.x < 0 || p.y < 0)
        return std::nullopt;
    auto pos = p.ToPosition();
    const auto& r = g.rect;
    if (pos.x < r.origin.x + margin || pos.x + margin >= r.origin.x + r.size.width || pos.y < r.origin.y + margin ||
        pos.y + margin >= r.origin.y + r.size.height)
        return std::nullopt;
    return GridWindow{
        .cells = g.data,
        .stride = static_cast<ptrdiff_t>(r.size.width),
        .base = static_cast<ptrdiff_t>(CellIndex(r, pos)),
    };
}

struct CollisionLookup {
    LevelGrid primary;
    std::unordered_map<uint32_t, LevelGrid> secondary;

    // Spatial-locality cache: once A* exits `primary` into a secondary grid,
    // subsequent probes repeatedly hit that same grid. `Get()` keeps this up
    // to date; `IsBlocked` / `GetPenalty` read it to skip the secondary-map scan.
    LevelGrid* lastHit = nullptr;

    // Point lookup. Checks primary, then secondary, then lazily loads adjacent levels.
    uint16_t Get(Point p);
    // Position overload - same lookup, skips the negative-coord guard.
    uint16_t Get(Position p);

    // 5-tile cross: center OR 4 cardinal neighbors
    uint16_t GetCross(Point p);

    // Wide cross: center OR 4 cardinal at distance 2
    uint16_t GetWide(Point p);

    // Blocked if (BLOCK_WALK | BLOCK_PLAYER) set in cross. Tries primary then
    // lastHit for one direct-array cross-OR via OpenWindow, falling back to
    // the generic per-probe IsBlockedSlow at grid boundaries.
    [[gnu::always_inline]] bool IsBlocked(Point p) {
        constexpr uint16_t MASK = collision::BLOCK_WALK | collision::BLOCK_PLAYER;
        if (auto w = OpenWindow(primary, p, 1))
            return (w->Cross() & MASK) != 0;
        if (lastHit != nullptr && lastHit != &primary) {
            if (auto w = OpenWindow(*lastHit, p, 1))
                return (w->Cross() & MASK) != 0;
        }
        return IsBlockedSlow(p);
    }

    // Generic per-probe IsBlocked. Called by IsBlocked() when the 3-cell range
    // around p straddles a grid edge; otherwise avoided entirely.
    bool IsBlockedSlow(Point p);

    // Walk-mode penalty: 50 (wide obstacle), 60 (object), 80 (closed door),
    // 10 (diagonal-adjacent obstacle), 0 (clear). One margin-2 OpenWindow
    // covers every probe the generic algorithm would make; falls back to
    // GetPenaltySlow when the 5x5 neighbourhood straddles a grid edge.
    [[gnu::always_inline]] int32_t GetPenalty(Point p) {
        auto attempt = [](const LevelGrid& g, Point p) -> std::optional<int32_t> {
            auto w = OpenWindow(g, p, 2);
            if (!w)
                return std::nullopt;
            constexpr uint16_t BLOCK = collision::BLOCK_WALK | collision::BLOCK_PLAYER;
            constexpr uint16_t ADJ = collision::OBJECT | collision::CLOSED_DOOR | collision::BLOCK_WALK;
            if ((w->Wide() & BLOCK) != 0)
                return 50;
            uint16_t cross = w->Cross();
            if ((cross & collision::OBJECT) != 0)
                return 60;
            if ((cross & collision::CLOSED_DOOR) != 0)
                return 80;
            // 8 cardinally-adjacent cross probes (diagonal-neighbour penalty).
            for (ptrdiff_t dy = -1; dy <= 1; dy++) {
                for (ptrdiff_t dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0)
                        continue;
                    if ((w->Cross(dx, dy) & ADJ) != 0)
                        return 10;
                }
            }
            return 0;
        };
        if (auto v = attempt(primary, p))
            return *v;
        if (lastHit != nullptr && lastHit != &primary) {
            if (auto v = attempt(*lastHit, p))
                return *v;
        }
        return GetPenaltySlow(p);
    }

    // Generic per-probe GetPenalty - fallback for the edge case in GetPenalty().
    int32_t GetPenaltySlow(Point p);

    // Relocate an invalid point to the nearest walkable spot (7x7 search)
    void MutatePoint(Point& pt);

    // Find which currently-loaded level contains `p`. Returns nullptr if `p` has
    // negative coords or falls outside every loaded level's rect.
    const LevelGrid* LevelOf(Point p) const;

    // Scan perimeter cells of every loaded level for walkable cells that abut a
    // walkable cell of a DIFFERENT level. Those cells are the entry/exit tiles the
    // teleport path has to pass through to move between levels.
    //
    // Same-level queries skip the scan entirely. Cross-level queries where the
    // destination level hasn't been lazy-loaded yet may return a partial list,
    // which degrades portal-heuristic quality but doesn't break correctness.
    std::vector<Point> FindPortals(Point start, Point end) const;
};

struct PathRequest {
    uint32_t areaId;
    Position start;
    Position end;
    ReductionType reduction = ReductionType::Walk;
    int32_t radius = 20;
    // Teleport A* heuristic weight. 1.0 = admissible (optimal cast count, slow).
    // Higher = faster search at the cost of up to `(weight-1) * 100`% extra distance.
    // Recommended values: 1.5 = balanced, 3.0 = fast production default (~60x speedup
    // vs 1.0 with <0.1% distance penalty on the cross-Kurast benchmark).
    double teleportHWeight = 3.0;
    std::stop_token cancelToken;

    std::function<bool(Position)> jsReject;
    std::function<std::vector<Position>(const std::vector<Position>& path)> jsReduce;
    std::function<Position(Position)> jsMutate;
};

std::vector<Position> FindPathOnGrid(
    CollisionLookup& collision, Position start, Position end, ReductionType reduction, int32_t radius,
    const std::stop_token& cancelToken, const std::function<bool(Position)>& jsReject = nullptr,
    const std::function<std::vector<Position>(const std::vector<Position>&)>& jsReduce = nullptr,
    const std::function<Position(Position)>& jsMutate = nullptr, double teleportHWeight = 3.0);

std::vector<Position> FindPath(const PathRequest& request);

// Flatten every room in `level` into one collision slab, applying the
// barricade-tower avoid overlay on the four Act 5 levels that use it.
// Reused by CollisionLookup (lazy secondary-grid load) and by the exit
// finder. Defined non-static so callers in other TUs can build a slab
// without duplicating the room walk + slab assembly.
LevelGrid BuildLevelGrid(d2bs::game::Level level);

}  // namespace d2bs::pathfinding
