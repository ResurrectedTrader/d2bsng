#include "components/pathfinding/Pathfinder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <ranges>
#include <unordered_set>

#include "game/GameHelpers.h"
#include "game/Level.h"
#include "game/Room.h"
#include "utils/VirtualArray.h"

namespace d2bs::pathfinding {

namespace {

// --- Heuristic / Distance ---

int32_t DiagonalShortcut(Point a, Point b) {
    int32_t dx = std::abs(a.x - b.x);
    int32_t dy = std::abs(a.y - b.y);
    return (dx > dy) ? (14 * dy) + (10 * (dx - dy)) : (14 * dx) + (10 * (dy - dx));
}

int32_t EuclideanDist(Point a, Point b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return static_cast<int32_t>(std::sqrt((dx * dx) + (dy * dy)) * 10);
}

int32_t SlopeFn(Point a, Point b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double slope = dy / dx;
    // NaN (0/0) and Inf (n/0) map to INT32_MIN, matching the x86 cvttsd2si
    // behavior that the reference Slope() relies on, but without UB.
    if (std::isnan(slope) || std::isinf(slope)) {
        return INT32_MIN;
    }
    return static_cast<int32_t>(slope);
}

// Portal-aware heuristic. When cur and end are in DIFFERENT loaded levels the
// path must exit cur's level via a portal, so h = min over portals-on-cur's-level
// of (EuclideanDist(cur, portal) + EuclideanDist(portal, end)). This retargets
// A* at the local exit portal first, then at the final goal once past it.
// Admissible. Falls back to straight-line Euclidean when levels match or when
// no portals for cur's level are known.
int32_t PortalH(Point cur, Point end, const std::vector<Point>& portals, const CollisionLookup& coll) {
    const auto* curL = coll.LevelOf(cur);
    const auto* endL = coll.LevelOf(end);
    if (!curL || !endL || curL == endL)
        return EuclideanDist(cur, end);
    int32_t best = std::numeric_limits<int32_t>::max();
    for (const auto& p : portals) {
        if (!curL->rect.Contains(p.ToPosition()))
            continue;
        int32_t h = EuclideanDist(cur, p) + EuclideanDist(p, end);
        best = std::min(h, best);
    }
    return (best == std::numeric_limits<int32_t>::max()) ? EuclideanDist(cur, end) : best;
}

}  // namespace

// --- LevelGrid ---

void LevelGrid::Set(Position p, uint16_t value) {
    if (!Contains(p))
        return;
    data[CellIndex(rect, p)] = value;
}

// --- CollisionLookup: portal detection for multi-level teleport paths ---

const LevelGrid* CollisionLookup::LevelOf(Point p) const {
    if (p.x < 0 || p.y < 0)
        return nullptr;
    auto pos = p.ToPosition();
    if (primary.rect.Contains(pos))
        return &primary;
    for (const auto& [id, g] : secondary) {
        if (g.rect.Contains(pos))
            return &g;
    }
    return nullptr;
}

std::vector<Point> CollisionLookup::FindPortals(Point start, Point end) const {
    const auto* sL = LevelOf(start);
    const auto* eL = LevelOf(end);
    if (sL && eL && sL == eL)
        return {};

    std::vector<const LevelGrid*> levels;
    levels.push_back(&primary);
    for (const auto& [id, g] : secondary)
        levels.push_back(&g);

    auto walkable = [](const LevelGrid& g, Point p) {
        if (p.x < 0 || p.y < 0)
            return false;
        auto pos = p.ToPosition();
        if (!g.rect.Contains(pos))
            return false;
        return (g.GetUnchecked(pos) & (collision::BLOCK_WALK | collision::BLOCK_PLAYER)) == 0;
    };

    std::vector<Point> portals;
    for (const auto* a : levels) {
        auto r = a->rect;
        auto x0 = static_cast<int32_t>(r.origin.x);
        auto x1 = static_cast<int32_t>(r.origin.x + r.size.width - 1);
        auto y0 = static_cast<int32_t>(r.origin.y);
        auto y1 = static_cast<int32_t>(r.origin.y + r.size.height - 1);
        auto check = [&](Point p) {
            if (!walkable(*a, p))
                return;
            std::array<Point, 4> dirs = {{{.x = 1, .y = 0}, {.x = -1, .y = 0}, {.x = 0, .y = 1}, {.x = 0, .y = -1}}};
            for (const auto& d : dirs) {
                Point np = p + d;
                for (const auto* b : levels) {
                    if (b == a)
                        continue;
                    if (walkable(*b, np)) {
                        portals.push_back(p);
                        return;
                    }
                }
            }
        };
        for (int32_t x = x0; x <= x1; x++) {
            check({.x = x, .y = y0});
            if (y0 != y1)
                check({.x = x, .y = y1});
        }
        for (int32_t y = y0 + 1; y < y1; y++) {
            check({.x = x0, .y = y});
            if (x0 != x1)
                check({.x = x1, .y = y});
        }
    }
    return portals;
}

// --- Build collision grid from game level ---

LevelGrid BuildLevelGrid(game::Level level) {
    LevelGrid grid(level.Bounds(), collision::AVOID);
    grid.levelId = level.Id();
    grid.mapSeed = game::GetMapSeed();

    for (auto room = level.GetFirstRoom(); room; room = room.GetNext()) {
        auto rb = room.Bounds();
        uint32_t roomW = rb.size.width;
        uint32_t roomH = rb.size.height;
        auto collData = room.GetCollisionFlat();
        if (roomW == 0 || roomH == 0 || collData.empty())
            continue;

        uint32_t rows = std::min(roomH, collData.size() / roomW);

        // Rooms are always fully within their level grid (D2 level layout invariant:
        // every room's origin/size is contained by pLevel->dwPosX/Y/SizeX/Y), so the
        // Position subtraction inside CellIndex cannot underflow here.
        for (uint32_t ry = 0; ry < rows; ry++) {
            Position dst{.x = rb.origin.x, .y = rb.origin.y + ry};
            std::memcpy(&grid.data[CellIndex(grid.rect, dst)], &collData[ry * roomW], roomW * sizeof(uint16_t));
        }
    }

    // Barricade tower avoidance for specific levels
    uint32_t levelId = level.Id();
    if (levelId == 74 || levelId == 111 || levelId == 112 || levelId == 117) {
        for (auto room = level.GetFirstRoom(); room; room = room.GetNext()) {
            auto roomPos = room.Bounds().origin;
            for (const auto& preset : room.GetPresetUnits()) {
                if (preset.id == 435) {
                    grid.Set(roomPos + preset.posInRoom, collision::AVOID);
                }
            }
        }
    }

    return grid;
}

// --- CollisionLookup members (after BuildLevelGrid) ---

uint16_t CollisionLookup::Get(Point p) {
    if (p.x < 0 || p.y < 0)
        return collision::AVOID;
    return Get(p.ToPosition());
}

uint16_t CollisionLookup::Get(Position p) {
    if (primary.Contains(p))
        return primary.GetUnchecked(p);
    if (lastHit != nullptr && lastHit->Contains(p))
        return lastHit->GetUnchecked(p);
    for (auto& [_, grid] : secondary) {
        if (grid.Contains(p)) {
            lastHit = &grid;
            return grid.GetUnchecked(p);
        }
    }
    auto level = d2bs::game::FindLevelAt(p);
    if (!level)
        return collision::AVOID;
    auto& grid = secondary[level.Id()] = BuildLevelGrid(level);
    if (grid.Contains(p)) {
        lastHit = &grid;
        return grid.GetUnchecked(p);
    }
    return collision::AVOID;
}

uint16_t CollisionLookup::GetCross(Point p) {
    return Get(p) | Get(Point{.x = p.x - 1, .y = p.y}) | Get(Point{.x = p.x + 1, .y = p.y}) |
           Get(Point{.x = p.x, .y = p.y - 1}) | Get(Point{.x = p.x, .y = p.y + 1});
}

uint16_t CollisionLookup::GetWide(Point p) {
    return Get(p) | Get(Point{.x = p.x - 2, .y = p.y}) | Get(Point{.x = p.x + 2, .y = p.y}) |
           Get(Point{.x = p.x, .y = p.y - 2}) | Get(Point{.x = p.x, .y = p.y + 2});
}

bool CollisionLookup::IsBlockedSlow(Point p) {
    return ((collision::BLOCK_WALK | collision::BLOCK_PLAYER) & GetCross(p)) != 0;
}

int32_t CollisionLookup::GetPenaltySlow(Point p) {
    if (((collision::BLOCK_WALK | collision::BLOCK_PLAYER) & GetWide(p)) != 0)
        return 50;
    uint16_t cross = GetCross(p);
    if ((cross & collision::OBJECT) != 0)
        return 60;
    if ((cross & collision::CLOSED_DOOR) != 0)
        return 80;

    // Light penalty for being near obstacles diagonally
    for (int32_t dx = -1; dx <= 1; dx++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0)
                continue;
            uint16_t adj = GetCross(Point{.x = p.x + dx, .y = p.y + dy});
            if ((adj & (collision::OBJECT | collision::CLOSED_DOOR | collision::BLOCK_WALK)) != 0) {
                return 10;
            }
        }
    }
    return 0;
}

void CollisionLookup::MutatePoint(Point& pt) {
    std::array<std::array<int32_t, 7>, 7> area{};
    for (int32_t i = -3; i <= 3; i++) {
        for (int32_t j = -3; j <= 3; j++) {
            if ((i == 0 && j == 0) || (std::abs(i) + std::abs(j)) == 6)
                continue;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            area[static_cast<size_t>(3 + i)][static_cast<size_t>(3 + j)] = Get(Point{.x = pt.x + i, .y = pt.y + j});
        }
    }
    constexpr uint16_t MASK = collision::BLOCK_WALK | collision::BLOCK_PLAYER;
    for (int32_t i = -2; i <= 2; i++) {
        for (int32_t j = -2; j <= 2; j++) {
            if ((i == 0 && j == 0) || std::abs(i + j) == 1)
                continue;
            auto ai = static_cast<size_t>(3 + i);
            auto aj = static_cast<size_t>(3 + j);
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
            auto combined = static_cast<uint16_t>(area[ai][aj] | area[ai + 1][aj] | area[ai - 1][aj] |
                                                  area[ai][aj + 1] | area[ai][aj - 1]);
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            if ((MASK & combined) == 0) {
                pt.x += i;
                pt.y += j;
                return;
            }
            j++;  // Reference quirk: skip next j on failure
        }
    }
}

namespace {

// --- Walk mode A* ---

// Node state is keyed by a monotonically-bumped generation counter rather
// than a plain `visited` bool. This lets the thread-local arena (see
// gArena below) be reused across calls without an O(N) zero-pass: at each
// WalkAStar entry we bump gCurrentGen, and any node whose stored
// `generation` doesn't match is treated as fresh by Touch() (resets g,
// parentIdx, closed, h). Value 0 is reserved for "never written" - the
// initial state of demand-paged VirtualAlloc memory - so gCurrentGen is
// always >= 1 when Touch() runs. Wraparound at 2^32 calls (~49 days at
// 1000 paths/sec) is handled at the bump site in WalkAStar().
struct WalkNode {
    uint32_t generation;
    int32_t parentIdx;
    int32_t g;
    int32_t h;
    bool closed;
};

struct OpenEntry {
    int32_t f;
    int32_t idx;
    bool operator>(const OpenEntry& o) const { return f > o.f; }
};

// Matches the reference's effective processing order: GetOpenNodes pushes
// E,W,S,N,SE,NW,NE,SW then AStarPath pops from back, reversing the order.
constexpr std::array<Point, 8> DIRECTIONS = {{
    {.x = -1, .y = 1},
    {.x = 1, .y = -1},
    {.x = -1, .y = -1},
    {.x = 1, .y = 1},
    {.x = 0, .y = -1},
    {.x = 0, .y = 1},
    {.x = -1, .y = 0},
    {.x = 1, .y = 0},
}};

// Per-thread single-slot arena cache for Walk A*.
//
// Rationale:
//   A level-sized VirtualArray<WalkNode> allocated per call dominates the
//   cost of short walks: on a 5-cell Kurast Flayer Jungle path the
//   VirtualAlloc + VirtualFree syscall pair plus first-touch demand-zero
//   faults cost ~15 us, vs ~1.5 us for the actual A* loop. Holding the
//   arena across calls removes both syscalls from the warm path; the
//   generation counter on WalkNode removes the need to zero between calls.
//   Measured: ~1.9 us warm vs reference ~10.5 us (5.5x) on the 5-cell bench.
//
// Sizing (per thread, ceiling):
//     level bounding rect                   sizeof(WalkNode)=20 bytes
//     ~120x120 indoor  (~14K cells)         ~280 KB
//     ~300x300 outdoor (~90K cells)         ~1.8 MB
//     320x960 Flayer Jungle (~307K cells)   ~6 MB
//     ~1024x1024 largest D2 levels (~1M)    ~20 MB  (worst case)
//   Actual resident memory stays below the ceiling because VirtualAlloc
//   pages are demand-zero: only cells A* has actually visited occupy
//   physical RAM. Over a long session the resident set plateaus at roughly
//   the area the player has roamed, a fraction of the full level rect.
//
// Implications for 32-bit:
//   The DLL targets Win32 (~2 GB user VA). 20 MB per thread is ~1% of VA;
//   N concurrent script threads scale linearly. Worth remembering if the
//   thread count grows, but not a concern in typical usage.
//
// Multi-level paths:
//   The cache is enabled only when both endpoints lie in the primary level.
//   If either endpoint is outside primary, the cache is disabled for the
//   whole call and every level uses a per-call VirtualArray (via
//   LevelNodes::ownedFallback). Cross-level A* already does enough work
//   that the extra allocations are rounding error; the benefit is a static
//   per-call policy - no mid-flight decisions about when it's safe to share
//   the arena.
//
// Thread safety:
//   All state is thread_local. No cross-thread synchronisation. CachedArena
//   holds a VirtualArray whose destructor calls VirtualFree; the runtime
//   invokes it on thread exit (including DLL_THREAD_DETACH), so memory is
//   reclaimed without explicit lifecycle management.
struct CachedArena {
    Position origin{};  // level bounding-rect origin of the cached arena
    utils::VirtualArray<WalkNode> data;
};

thread_local CachedArena gArena;
thread_local uint32_t gCurrentGen = 0;

// Per-call book-keeping for a single level's node storage. `nodes` is a
// non-owning pointer: it either references the thread-local gArena (the
// common single-level case) or points into `ownedFallback` on this struct
// (the cross-level case, when the cached arena is already claimed by an
// earlier level in this same call). Using a raw pointer keeps Touch()'s
// addressing identical on both paths.
struct LevelNodes {
    Rect rect;
    size_t index;
    WalkNode* nodes;                              // points into gArena.data or ownedFallback
    utils::VirtualArray<WalkNode> ownedFallback;  // non-empty only for fallback case

    bool Contains(Position p) const { return rect.Contains(p); }
    int32_t ToIdx(Position p) const { return static_cast<int32_t>(CellIndex(rect, p)); }
    Position ToPosition(int32_t idx) const {
        auto uidx = static_cast<uint32_t>(idx);
        return {.x = (uidx % rect.size.width) + rect.origin.x, .y = (uidx / rect.size.width) + rect.origin.y};
    }
    // Point form for the A* inner loop, where `cur + dir` requires signed arithmetic.
    Point ToPoint(int32_t idx) const { return ToPosition(idx).ToPoint(); }

    WalkNode& Touch(int32_t idx) const {
        auto& n = nodes[idx];
        if (n.generation != gCurrentGen) {
            n.generation = gCurrentGen;
            n.g = std::numeric_limits<int32_t>::max();
            n.parentIdx = -1;
            n.closed = false;
            n.h = 0;
        }
        return n;
    }
};

struct NodeRef {
    size_t levelIdx;
    int32_t localIdx;
};

std::vector<Position> WalkAStar(CollisionLookup& coll, Position start, Position end, bool usePenalties,
                                const std::stop_token& cancelToken, const std::function<bool(Position)>& jsReject) {
    // Bump the generation counter so Touch() treats previously-stored node
    // state in the cached arena as stale. On wrap (0 is reserved for the
    // fresh-page sentinel) drop the arena - at ~1000 paths/sec this happens
    // roughly every 49 days, far too infrequent for the free cost to matter.
    if (++gCurrentGen == 0) {
        gArena.data = d2bs::utils::VirtualArray<WalkNode>{};
        gCurrentGen = 1;
    }

    // A* neighbor math operates on signed Point (for diagonal offset arithmetic
    // and short-circuit checks near grid edges); convert the unsigned Position
    // endpoints once at entry and use Point internally.
    Point startPt = start.ToPoint();
    Point endPt = end.ToPoint();

    // jsReject takes ownership of the mutate decision (the JSCallback path in
    // FindPathOnGrid invokes jsMutate before calling us). Only when no jsReject
    // is supplied do we run the built-in MutatePoint. Sync the mutated Point
    // copies back into the Position aliases so ToIdx/getLevel see them.
    if (!jsReject) {
        if (coll.IsBlocked(startPt))
            coll.MutatePoint(startPt);
        if (coll.IsBlocked(endPt))
            coll.MutatePoint(endPt);
        start = startPt.ToPosition();
        end = endPt.ToPosition();
    }

    // Arena is used only when both endpoints lie in the primary level. That's
    // the common case (script-driven intra-level pathing) and makes the cache
    // policy static per-call: no mid-flight bookkeeping to decide when it's
    // safe to share the arena. Cross-level paths fall back to per-call
    // VirtualArrays for every level they touch - the A* loop itself is large
    // enough in that case that the extra allocations are noise.
    bool useArena = coll.primary.Contains(start) && coll.primary.Contains(end);

    // Per-level node storage. deque for stable references on push_back.
    // Each level's VirtualArray is demand-paged: only pages the A* touches
    // get committed to physical memory.
    std::deque<LevelNodes> allLevels;

    auto addLevel = [&](const LevelGrid& g) -> LevelNodes* {
        // Level already registered earlier in this same call? Return that entry.
        for (auto& ln : allLevels)
            if (ln.rect.origin == g.rect.origin)
                return &ln;

        size_t area = g.rect.size.Area();

        // Arena path: only when this is a single-level call (useArena) AND
        // this is the first level we're registering. Hit (same level as the
        // last call on this thread) reuses the existing VirtualAlloc region;
        // the generation bump at entry makes stored node state appear fresh
        // on first Touch(). Miss evicts + reallocates.
        if (useArena && allLevels.empty()) {
            if (gArena.data.Empty() || gArena.origin != g.rect.origin || gArena.data.Size() != area) {
                utils::VirtualArray<WalkNode> arr(area);
                if (arr.Empty())
                    return nullptr;
                gArena = {.origin = g.rect.origin, .data = std::move(arr)};
            }
            allLevels.push_back({.rect = g.rect, .index = allLevels.size(), .nodes = gArena.data.Data()});
            return &allLevels.back();
        }

        // Per-call fallback: transient VirtualArray owned by this LevelNodes
        // and freed when allLevels destructs at WalkAStar exit.
        utils::VirtualArray<WalkNode> arr(area);
        if (arr.Empty())
            return nullptr;
        WalkNode* raw = arr.Data();
        allLevels.push_back({.rect = g.rect, .index = allLevels.size(), .nodes = raw, .ownedFallback = std::move(arr)});
        return &allLevels.back();
    };

    auto getLevel = [&](Position pos) -> LevelNodes* {
        for (auto& ln : allLevels) {
            if (ln.Contains(pos))
                return &ln;
        }
        if (coll.primary.Contains(pos))
            return addLevel(coll.primary);
        for (auto& [id, g] : coll.secondary) {
            if (g.Contains(pos))
                return addLevel(g);
        }
        coll.Get(pos);  // trigger lazy level loading
        for (auto& [id, g] : coll.secondary) {
            if (g.Contains(pos))
                return addLevel(g);
        }
        return nullptr;
    };

    auto* startLevel = getLevel(start);
    auto* endLevel = getLevel(end);
    if (!startLevel || !endLevel)
        return {};

    std::vector<NodeRef> nodeRefs;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<>> open;

    int32_t startLocalIdx = startLevel->ToIdx(start);
    auto& startNode = startLevel->Touch(startLocalIdx);
    startNode.g = 0;
    startNode.h = DiagonalShortcut(startPt, endPt);
    int32_t startRefIdx = static_cast<int32_t>(nodeRefs.size());
    nodeRefs.push_back({.levelIdx = startLevel->index, .localIdx = startLocalIdx});
    open.push({startNode.g + startNode.h, startRefIdx});

    int32_t endLocalIdx = endLevel->ToIdx(end);
    uint32_t iterations = 0;
    int32_t foundRefIdx = -1;

    while (!open.empty()) {
        if ((++iterations & 0xFF) == 0 && cancelToken.stop_requested())
            return {};

        auto [curF, curRefIdx] = open.top();
        open.pop();

        auto& curRef = nodeRefs[curRefIdx];
        auto& curLevel = allLevels[curRef.levelIdx];
        auto& curNode = curLevel.Touch(curRef.localIdx);
        if (curNode.closed)
            continue;
        curNode.closed = true;

        Point cur = curLevel.ToPoint(curRef.localIdx);
        if (&curLevel == endLevel && curRef.localIdx == endLocalIdx) {
            foundRefIdx = curRefIdx;
            break;
        }

        for (const auto& dir : DIRECTIONS) {
            Point np = cur + dir;

            // A* neighbor stepping can land on negative coords near the grid
            // origin. Short-circuit so np.ToPosition() is safe and jsReject
            // only ever sees non-negative Positions.
            if (np.x < 0 || np.y < 0)
                continue;

            Position npPos = np.ToPosition();

            if (np != endPt && (jsReject ? jsReject(npPos) : coll.IsBlocked(np)))
                continue;

            auto* nLevel = curLevel.Contains(npPos) ? &curLevel : getLevel(npPos);
            if (!nLevel)
                continue;

            int32_t nLocalIdx = nLevel->ToIdx(npPos);
            auto& neighbor = nLevel->Touch(nLocalIdx);
            if (neighbor.closed)
                continue;

            int32_t penalty = usePenalties ? coll.GetPenalty(np) : 0;
            int32_t newG = curNode.g + (dir.x && dir.y ? 14 : 10) + penalty;
            if (newG < neighbor.g) {
                neighbor.g = newG;
                neighbor.h = DiagonalShortcut(np, endPt);
                neighbor.parentIdx = curRefIdx;
                int32_t newRefIdx = static_cast<int32_t>(nodeRefs.size());
                nodeRefs.push_back({.levelIdx = nLevel->index, .localIdx = nLocalIdx});
                open.push({newG + neighbor.h, newRefIdx});
            }
        }
    }

    if (foundRefIdx < 0)
        return {};

    std::vector<Position> path;
    int32_t refIdx = foundRefIdx;
    while (refIdx >= 0) {
        auto& ref = nodeRefs[refIdx];
        path.push_back(allLevels[ref.levelIdx].ToPosition(ref.localIdx));
        refIdx = allLevels[ref.levelIdx].nodes[ref.localIdx].parentIdx;
    }
    std::ranges::reverse(path);
    return path;
}

// --- Walk Reduction (slope-based) ---

std::vector<Position> WalkReduce(const std::vector<Position>& in, int32_t radius) {
    if (in.size() < 2)
        return in;
    int32_t range = radius * 10;
    std::vector<Position> out;

    // Slope and Euclidean operate in signed Point for NaN/Inf handling and to
    // match the reference's semantics; convert at each use site.
    // Mirrors reference WalkPathReducer::Reduce, but avoids UB from Slope(p, p).
    // Reference iteration 1: init=true, Slope(in[0],in[0]) = 0/0 -> (int)NaN which happens
    // to be 0 on the target platform. So: push in[0], set slope = 0.
    // We replicate this by pushing in[0] and initializing slope to 0 explicitly.
    out.push_back(in.front());
    Position last = in.front();
    Position first = in.front();
    int32_t slope = 0;

    for (auto it = in.begin() + 1; it != in.end() - 1; ++it) {
        Position next = *it;
        int32_t slopeNext = SlopeFn(first.ToPoint(), next.ToPoint());
        if (slopeNext != slope) {
            out.push_back(first);
            last = first;
            slope = slopeNext;
        } else if (EuclideanDist(last.ToPoint(), next.ToPoint()) >= range) {
            out.push_back(first);
            last = first;
        }
        first = next;
    }
    out.push_back(in.back());
    return out;
}

// --- Teleport mode: coarse jump A* ---

struct TeleportNode {
    Point parent = Point::Zero;
    int32_t g = std::numeric_limits<int32_t>::max();
    int32_t h = 0;
    int32_t pointIdx = -1;
    bool hasParent = false;
};

// Ring of candidate offsets at distance ~`radius` from the origin. Built once per
// query in CastCountTeleportSearch.
std::vector<Point> BuildTeleportRing(int32_t radius) {
    int32_t range = radius * 10;
    std::vector<Point> ring;
    ring.reserve(8 * radius);
    for (int32_t x = -radius; x <= radius; x++) {
        for (int32_t y = -radius; y <= radius; y++) {
            int32_t d = EuclideanDist({.x = x, .y = y}, Point::Zero);
            if (d < range && d > range - 5) {
                ring.push_back({.x = x, .y = y});
            }
        }
    }
    return ring;
}

// Weighted f-value: f = g + h*w. Centralises the one cast needed.
int32_t WeightedF(int32_t g, int32_t h, double hWeight) {
    return g + static_cast<int32_t>(h * hWeight);
}

// CastCountTeleportSearch - Jump-A*-style search optimising cast count (not travel
// distance) with portal-aware heuristic and weighted h.
//
// Key differences from a distance-minimising A*:
//   * g is UNIFORM cast cost (each jump adds `range`, regardless of actual distance),
//     so the search optimises cast count rather than total travel distance.
//   * h uses PortalH when start/end levels differ - heuristic retargets at the local
//     exit portal while cur is in the wrong level, then at the goal once past it.
//   * Ring-candidate selection picks the candidate that MINIMISES THE HEURISTIC,
//     not plain straight-line distance to end, so the ring naturally aims at the
//     portal centroid when cross-level.
//   * `hWeight > 1` trades optimality for speed. 1.5 is balanced, 3.0 is the
//     production default (~60x speedup vs 1.0 with <0.1% distance penalty).
std::vector<Position> CastCountTeleportSearch(CollisionLookup& coll, Position start, Position end, int32_t radius,
                                              const std::stop_token& cancelToken, double hWeight) {
    // Ring offsets and A* neighbor math stay in signed Point; convert the
    // unsigned Position endpoints once at entry.
    Point startPt = start.ToPoint();
    Point endPt = end.ToPoint();

    const int32_t range = radius * 10;
    const int32_t earlyGoalRange = range - 20;
    const std::vector<Point> ring = BuildTeleportRing(radius);
    const std::vector<Point> portals = coll.FindPortals(startPt, endPt);

    if (coll.IsBlocked(startPt))
        coll.MutatePoint(startPt);
    if (coll.IsBlocked(endPt))
        coll.MutatePoint(endPt);

    auto heuristic = [&](Point p) -> int32_t {
        return portals.empty() ? EuclideanDist(p, endPt) : PortalH(p, endPt, portals, coll);
    };

    // Squared-distance rank used for ring top-K selection and bestPtSoFar tracking.
    // Preserves the same ordering as Euclidean (monotonic for non-negative values)
    // without paying sqrt 160x per expansion. Sqrt is only needed for the K=4
    // candidates we actually push (via heuristic() inside pushNeighbor).
    auto rank = [&](Point p) -> int32_t {
        if (portals.empty()) {
            const int32_t dx = p.x - endPt.x;
            const int32_t dy = p.y - endPt.y;
            return (dx * dx) + (dy * dy);
        }
        return PortalH(p, endPt, portals, coll);
    };

    std::unordered_map<Point, TeleportNode> nodes;
    std::unordered_set<Point> closed;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<>> open;
    std::vector<Point> nodePoints;

    auto getOrCreate = [&](Point p) -> std::pair<int32_t, TeleportNode&> {
        auto [it, inserted] = nodes.try_emplace(p);
        if (inserted) {
            it->second.pointIdx = static_cast<int32_t>(nodePoints.size());
            nodePoints.push_back(p);
        }
        return {it->second.pointIdx, it->second};
    };

    auto pushNeighbor = [&](Point np, int32_t parentG, Point parent) {
        if (closed.contains(np))
            return;
        auto [npIdx, npNode] = getOrCreate(np);
        const int32_t newG = parentG + range + EuclideanDist(parent, np);
        if (newG >= npNode.g)
            return;
        npNode.g = newG;
        npNode.h = heuristic(np);
        npNode.parent = parent;
        npNode.hasParent = true;
        open.push({WeightedF(newG, npNode.h, hWeight), npIdx});
    };

    auto [startIdx, startNode] = getOrCreate(startPt);
    startNode.g = 0;
    startNode.h = heuristic(startPt);
    open.push({WeightedF(startNode.g, startNode.h, hWeight), startIdx});

    uint32_t iterations = 0;
    bool found = false;

    // Tracks the node closest to the goal that the search has so far reached
    // through ring jumps. Branches that wander far from this anchor skip the
    // expensive ring scan and fall through to 8-walk only - limiting useless
    // exploration in dead-end pockets. Mirrors the reference's `bestPtSoFar`.
    Point bestPtSoFar = startPt;
    int32_t bestPtSoFarH = rank(startPt);
    constexpr int32_t BEST_PT_GATE = 500;

    while (!open.empty()) {
        if ((++iterations & 0xFF) == 0 && cancelToken.stop_requested())
            return {};

        auto [_, curIdx] = open.top();
        open.pop();

        const Point cur = nodePoints[curIdx];
        if (!closed.insert(cur).second)
            continue;
        if (cur == endPt) {
            found = true;
            break;
        }

        // Copy g - getOrCreate() below may rehash `nodes`, invalidating references.
        const int32_t curG = nodes[cur].g;

        // Early-out: if the goal is within a single cast, jump straight to it.
        if (EuclideanDist(endPt, cur) < earlyGoalRange && !coll.IsBlocked(endPt)) {
            pushNeighbor(endPt, curG, cur);
            continue;
        }

        // Skip ring search entirely when this node has wandered too far from the
        // best progress we've seen - these branches are usually dead-ends and
        // pay for their ring scan with no contribution. Fall through to 8-walk.
        if (EuclideanDist(bestPtSoFar, cur) >= BEST_PT_GATE) {
            for (const auto& dir : DIRECTIONS) {
                const Point np = cur + dir;
                if (!coll.IsBlocked(np))
                    pushNeighbor(np, curG, cur);
            }
            continue;
        }

        // Ring search: keep the top-K candidates by squared-distance rank (see
        // `rank` lambda above). Pushing more than the single best lets A* recover
        // when the locally-greedy choice leads to a globally worse path.
        constexpr size_t TOP_K = 4;
        std::array<std::pair<int32_t, Point>, TOP_K> topK;
        size_t topCount = 0;

        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
        for (const auto& r : ring) {
            const Point np = cur + r;
            if (coll.IsBlocked(np) || closed.contains(np))
                continue;
            const int32_t h = rank(np);

            // Skip when topK is full and h is worse than the current Kth-best.
            if (topCount == TOP_K && h >= topK[TOP_K - 1].first)
                continue;

            size_t pos = topCount < TOP_K ? topCount : TOP_K - 1;
            while (pos > 0 && topK[pos - 1].first > h) {
                topK[pos] = topK[pos - 1];
                --pos;
            }
            topK[pos] = {h, np};
            if (topCount < TOP_K)
                ++topCount;
        }

        const int32_t curRank = rank(cur);
        bool pushedAny = false;
        for (size_t i = 0; i < topCount; ++i) {
            if (topK[i].first < curRank) {
                pushNeighbor(topK[i].second, curG, cur);
                pushedAny = true;
            }
        }
        if (pushedAny) {
            // topK is sorted ascending by h, so topK[0] is our most-promising push.
            // Anchor bestPtSoFar there if it advances over the previous anchor.
            if (topK[0].first < bestPtSoFarH) {
                bestPtSoFar = topK[0].second;
                bestPtSoFarH = topK[0].first;
            }
            continue;
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

        // Stuck: fall back to 8-way walk moves to make progress out of dead ends.
        for (const auto& dir : DIRECTIONS) {
            const Point np = cur + dir;
            if (!coll.IsBlocked(np))
                pushNeighbor(np, curG, cur);
        }
    }

    if (!found)
        return {};

    // The explored-node points all come from endPt or endPt + ring offsets where
    // the offset was filtered by coll.IsBlocked (which rejects negatives for us).
    // Walking the parent chain from endPt therefore only visits non-negative cells,
    // so .ToPosition() is safe on every element.
    std::vector<Position> path;
    Point cur = endPt;
    while (true) {
        path.push_back(cur.ToPosition());
        const auto& node = nodes[cur];
        if (!node.hasParent)
            break;
        cur = node.parent;
    }
    std::ranges::reverse(path);
    return path;
}

}  // anonymous namespace

// --- Public API ---

std::vector<Position> FindPathOnGrid(CollisionLookup& collision, Position start, Position end, ReductionType reduction,
                                     int32_t radius, const std::stop_token& cancelToken,
                                     const std::function<bool(Position)>& jsReject,
                                     const std::function<std::vector<Position>(const std::vector<Position>&)>& jsReduce,
                                     const std::function<Position(Position)>& jsMutate, double teleportHWeight) {
    switch (reduction) {
        case ReductionType::Walk: {
            auto raw = WalkAStar(collision, start, end, true, cancelToken, nullptr);
            if (raw.empty())
                return {};
            return WalkReduce(raw, radius);
        }
        case ReductionType::Teleport:
            return CastCountTeleportSearch(collision, start, end, radius, cancelToken, teleportHWeight);
        case ReductionType::None:
            return WalkAStar(collision, start, end, false, cancelToken, nullptr);
        case ReductionType::JSCallback: {
            Position s = start;
            Position e = end;
            if (jsReject && jsReject(s) && jsMutate)
                s = jsMutate(s);
            if (jsReject && jsReject(e) && jsMutate)
                e = jsMutate(e);
            auto result = WalkAStar(collision, s, e, false, cancelToken, jsReject);
            if (!result.empty() && jsReduce) {
                result = jsReduce(result);
            }
            return result;
        }
    }
    return {};
}

// Per-thread cache of the most recently built CollisionLookup.
//
// BuildLevelGrid walks every room in a level and copies each collision
// grid into a flat slab - for Blood Moor (~50 rooms) that's ~50 boundary
// calls into backends/lod114d/game/Room.cpp::GetCollisionFlat per construction. Bot
// helpers like Attack.getIntoPosition fan out 45+ pathfinds in a single
// burst against the same level; rebuilding the slab each time is what
// turned an in-level scan into a 20-second pause (reported with
// walkDist=19710ms in the verbose trace).
//
// Reference d2bs sidesteps this by holding one ActMap instance across all
// pathfind calls and reusing the cached collision view. Mirror that with
// a thread_local cache keyed by (levelId, mapSeed) on the slab: same-
// level calls within the same game reuse the slab + any lazy-loaded
// secondary grids; a level change OR a game change (new map seed) evicts.
// The seed check is what makes this safe across game leaves/joins - D2
// regenerates each level per game, so the same levelId in two different
// games points at different room layouts.
//
// The cache is per-thread so concurrent script threads don't contend, and
// because each LevelGrid is a self-contained vector<uint16_t> copy of the
// game's collision masks, it's safe to keep without holding a GameReadLock
// outside the build path.
//
// Staleness window: doors opening / collision changes inside the same
// level aren't reflected - accepted, getWalkDistance is a heuristic
// check, and A* on actual traversal goes through the cache eviction or
// re-resolve path on each fresh call sequence.
namespace {
thread_local std::optional<CollisionLookup> cachedLookup;
}  // namespace

std::vector<Position> FindPath(const PathRequest& request) {
    const uint32_t currentSeed = game::GetMapSeed();
    const bool cacheValid = cachedLookup.has_value() && cachedLookup->primary.levelId == request.areaId &&
                            cachedLookup->primary.mapSeed == currentSeed;
    if (!cacheValid) {
        auto levelOpt = game::Level::Get(request.areaId);
        if (!levelOpt)
            return {};
        cachedLookup.emplace();
        cachedLookup->primary = BuildLevelGrid(*levelOpt);
    }

    return FindPathOnGrid(*cachedLookup, request.start, request.end, request.reduction, request.radius,
                          request.cancelToken, request.jsReject, request.jsReduce, request.jsMutate,
                          request.teleportHWeight);
}

}  // namespace d2bs::pathfinding
