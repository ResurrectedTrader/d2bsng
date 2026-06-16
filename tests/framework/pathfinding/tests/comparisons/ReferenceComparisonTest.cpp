// Reference comparison tests: run the reference A* on the same collision grids
// as our implementation and verify they produce equivalent paths.
//
// Both implementations use the same DiagonalShortcut heuristic, same penalty
// model (50/60/80/10), and matched direction order. Our code uses per-level
// demand-paged flat arrays; the reference uses heap-allocated nodes.
//
// On unambiguous paths (straight lines, corridors), both produce identical output.
// On ambiguous paths (obstacle detours), different node storage causes different
// tie-breaking, producing different but equally-optimal paths (same cost).

#include <cmath>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"
#include "pathfinding/reference/AStarPath.h"
#include "pathfinding/reference/NoPathReducer.h"
#include "pathfinding/reference/WalkPathReducer.h"

using namespace d2bs::pathfinding;

// --- Helpers ---

// Run reference A* with NoPathReducer (no penalties, no reduction)
static std::vector<Point> RunReferenceNone(const LevelGrid& grid, Point start, Point end) {
    auto map = std::make_shared<TestActMap>(&grid);
    auto reducer = std::make_unique<Mapping::Pathing::Reducing::NoPathReducer>(map);
    Mapping::Pathing::AStarPath<> pather(map, std::move(reducer));
    std::vector<Point> result;
    pather.GetPath(start, end, result);
    return result;
}

// Run reference A* with WalkPathReducer (with penalties and slope-based reduction)
static std::vector<Point> RunReferenceWalk(const LevelGrid& grid, Point start, Point end, int32_t radius) {
    auto map = std::make_shared<TestActMap>(&grid);
    auto reducer = std::make_unique<Mapping::Pathing::Reducing::WalkPathReducer>(map, radius);
    Mapping::Pathing::AStarPath<> pather(map, std::move(reducer));
    std::vector<Point> result;
    pather.GetPath(start, end, result);
    return result;
}

// Run our pathfinder with ReductionType::None (raw A*, no penalties)
// Returns Position; callers convert to Point for equality with reference results.
static std::vector<Position> RunOursNone(LevelGrid& grid, Position start, Position end) {
    CollisionLookup coll;
    coll.primary = grid;
    return FindPathOnGrid(coll, start, end, ReductionType::None, 20, {});
}

// Run our pathfinder with ReductionType::Walk (penalties + slope reduction)
static std::vector<Position> RunOursWalk(LevelGrid& grid, Position start, Position end, int32_t radius) {
    CollisionLookup coll;
    coll.primary = grid;
    return FindPathOnGrid(coll, start, end, ReductionType::Walk, radius, {});
}

// Convert a Position-vector into a Point-vector for element-wise comparison
// against reference output. All path cells are non-negative by construction.
static std::vector<Point> ToPoints(const std::vector<Position>& in) {
    std::vector<Point> out;
    out.reserve(in.size());
    for (const auto& p : in)
        out.push_back(p.ToPoint());
    return out;
}

// Check exact point-by-point match
static void CheckPathsMatch(const std::vector<Position>& ours, const std::vector<Point>& ref) {
    auto oursPts = ToPoints(ours);
    CHECK(oursPts.size() == ref.size());
    for (size_t i = 0; i < std::min(oursPts.size(), ref.size()); i++) {
        CHECK(oursPts[i] == ref[i]);
    }
}

// Compute total path cost using DiagonalShortcut (same as both implementations)
static int ComputePathCost(const std::vector<Point>& path) {
    int cost = 0;
    for (size_t i = 1; i < path.size(); i++) {
        int dx = std::abs(path[i].x - path[i - 1].x);
        int dy = std::abs(path[i].y - path[i - 1].y);
        cost += (dx > dy) ? (14 * dy) + (10 * (dx - dy)) : (14 * dx) + (10 * (dy - dx));
    }
    return cost;
}

// Check endpoints match and path costs are equal (for cases with tie-breaking differences)
static void CheckPathsEquivalent(const std::vector<Position>& ours, const std::vector<Point>& ref) {
    REQUIRE_FALSE(ours.empty());
    REQUIRE_FALSE(ref.empty());
    auto oursPts = ToPoints(ours);
    // Start points match
    CHECK(oursPts.front() == ref.front());
    // End points match
    CHECK(oursPts.back() == ref.back());
    // Same total cost
    CHECK(ComputePathCost(oursPts) == ComputePathCost(ref));
}

// ============================================================
// Raw A* (no reduction, no penalties)
// Unambiguous paths (unique shortest path) -> exact match
// Ambiguous paths (multiple equal-cost shortest) -> cost match
// ============================================================

TEST_SUITE("Reference comparison: raw A*") {
    TEST_CASE("open grid diagonal -- exact match") {
        LevelGrid grid({.size = {.width = 100, .height = 100}});
        auto ours = RunOursNone(grid, {.x = 10, .y = 10}, {.x = 90, .y = 90});
        auto ref = RunReferenceNone(grid, {.x = 10, .y = 10}, {.x = 90, .y = 90});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("open grid horizontal -- exact match") {
        LevelGrid grid({.size = {.width = 100, .height = 100}});
        auto ours = RunOursNone(grid, {.x = 10, .y = 50}, {.x = 90, .y = 50});
        auto ref = RunReferenceNone(grid, {.x = 10, .y = 50}, {.x = 90, .y = 50});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("open grid vertical -- exact match") {
        LevelGrid grid({.size = {.width = 100, .height = 100}});
        auto ours = RunOursNone(grid, {.x = 50, .y = 10}, {.x = 50, .y = 90});
        auto ref = RunReferenceNone(grid, {.x = 50, .y = 10}, {.x = 50, .y = 90});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("narrow corridor -- exact match") {
        // 3-tile-wide corridor: only one sensible path (straight through)
        LevelGrid grid({.size = {.width = 40, .height = 40}}, collision::BLOCK_WALK);
        for (uint32_t x = 0; x < 40; x++) {
            grid.Set({.x = x, .y = 19}, 0);
            grid.Set({.x = x, .y = 20}, 0);
            grid.Set({.x = x, .y = 21}, 0);
        }

        auto ours = RunOursNone(grid, {.x = 5, .y = 20}, {.x = 35, .y = 20});
        auto ref = RunReferenceNone(grid, {.x = 5, .y = 20}, {.x = 35, .y = 20});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("adjacent points -- exact match") {
        LevelGrid grid({.size = {.width = 20, .height = 20}});
        auto ours = RunOursNone(grid, {.x = 10, .y = 10}, {.x = 11, .y = 10});
        auto ref = RunReferenceNone(grid, {.x = 10, .y = 10}, {.x = 11, .y = 10});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("diagonal adjacent -- exact match") {
        LevelGrid grid({.size = {.width = 20, .height = 20}});
        auto ours = RunOursNone(grid, {.x = 10, .y = 10}, {.x = 11, .y = 11});
        auto ref = RunReferenceNone(grid, {.x = 10, .y = 10}, {.x = 11, .y = 11});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("start equals end -- exact match") {
        LevelGrid grid({.size = {.width = 20, .height = 20}});
        auto ours = RunOursNone(grid, {.x = 10, .y = 10}, {.x = 10, .y = 10});
        auto ref = RunReferenceNone(grid, {.x = 10, .y = 10}, {.x = 10, .y = 10});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("non-zero origin -- exact match") {
        LevelGrid grid({.origin = {.x = 1000, .y = 2000}, .size = {.width = 50, .height = 50}});
        auto ours = RunOursNone(grid, {.x = 1010, .y = 2010}, {.x = 1040, .y = 2040});
        auto ref = RunReferenceNone(grid, {.x = 1010, .y = 2010}, {.x = 1040, .y = 2040});
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("unreachable destination -- both empty") {
        LevelGrid grid({.size = {.width = 20, .height = 10}});
        for (uint32_t y = 0; y < 10; y++)
            grid.Set({.x = 10, .y = y}, collision::BLOCK_WALK);

        auto ours = RunOursNone(grid, {.x = 5, .y = 5}, {.x = 15, .y = 5});
        auto ref = RunReferenceNone(grid, {.x = 5, .y = 5}, {.x = 15, .y = 5});
        CHECK(ours.empty());
        CHECK(ref.empty());
    }

    // --- Obstacle tests: cost equivalence (tie-breaking may differ) ---

    TEST_CASE("vertical wall -- equivalent cost") {
        LevelGrid grid({.size = {.width = 50, .height = 50}});
        for (uint32_t y = 0; y < 40; y++)
            grid.Set({.x = 25, .y = y}, collision::BLOCK_WALK);

        auto ours = RunOursNone(grid, {.x = 10, .y = 25}, {.x = 40, .y = 25});
        auto ref = RunReferenceNone(grid, {.x = 10, .y = 25}, {.x = 40, .y = 25});
        CheckPathsEquivalent(ours, ref);
    }

    TEST_CASE("horizontal wall -- equivalent cost") {
        LevelGrid grid({.size = {.width = 50, .height = 50}});
        for (uint32_t x = 0; x < 40; x++)
            grid.Set({.x = x, .y = 25}, collision::BLOCK_WALK);

        auto ours = RunOursNone(grid, {.x = 25, .y = 10}, {.x = 25, .y = 40});
        auto ref = RunReferenceNone(grid, {.x = 25, .y = 10}, {.x = 25, .y = 40});
        CheckPathsEquivalent(ours, ref);
    }

    TEST_CASE("L-shaped obstacle -- equivalent cost") {
        LevelGrid grid({.size = {.width = 50, .height = 50}});
        for (uint32_t x = 10; x <= 35; x++)
            grid.Set({.x = x, .y = 20}, collision::BLOCK_WALK);
        for (uint32_t y = 20; y <= 45; y++)
            grid.Set({.x = 35, .y = y}, collision::BLOCK_WALK);

        auto ours = RunOursNone(grid, {.x = 20, .y = 15}, {.x = 40, .y = 30});
        auto ref = RunReferenceNone(grid, {.x = 20, .y = 15}, {.x = 40, .y = 30});
        CheckPathsEquivalent(ours, ref);
    }

    TEST_CASE("maze-like grid -- equivalent cost") {
        LevelGrid grid({.size = {.width = 30, .height = 30}});
        for (uint32_t x = 5; x < 25; x++)
            grid.Set({.x = x, .y = 10}, collision::BLOCK_WALK);
        grid.Set({.x = 24, .y = 10}, 0);  // gap at right end
        for (uint32_t x = 5; x < 25; x++)
            grid.Set({.x = x, .y = 20}, collision::BLOCK_WALK);
        grid.Set({.x = 5, .y = 20}, 0);  // gap at left end

        auto ours = RunOursNone(grid, {.x = 15, .y = 5}, {.x = 15, .y = 25});
        auto ref = RunReferenceNone(grid, {.x = 15, .y = 5}, {.x = 15, .y = 25});
        CheckPathsEquivalent(ours, ref);
    }

    TEST_CASE("U-shaped channel -- equivalent cost") {
        LevelGrid grid({.size = {.width = 40, .height = 40}});
        // U shape: walls on three sides forcing a detour
        for (uint32_t x = 10; x <= 30; x++)
            grid.Set({.x = x, .y = 10}, collision::BLOCK_WALK);
        for (uint32_t y = 10; y <= 30; y++)
            grid.Set({.x = 10, .y = y}, collision::BLOCK_WALK);
        for (uint32_t x = 10; x <= 30; x++)
            grid.Set({.x = x, .y = 30}, collision::BLOCK_WALK);

        auto ours = RunOursNone(grid, {.x = 20, .y = 20}, {.x = 35, .y = 20});
        auto ref = RunReferenceNone(grid, {.x = 20, .y = 20}, {.x = 35, .y = 20});
        CheckPathsEquivalent(ours, ref);
    }

    TEST_CASE("multiple obstacles -- equivalent cost") {
        LevelGrid grid({.size = {.width = 60, .height = 60}});
        // Scattered walls
        for (uint32_t y = 5; y < 35; y++)
            grid.Set({.x = 15, .y = y}, collision::BLOCK_WALK);
        for (uint32_t y = 25; y < 55; y++)
            grid.Set({.x = 30, .y = y}, collision::BLOCK_WALK);
        for (uint32_t y = 5; y < 45; y++)
            grid.Set({.x = 45, .y = y}, collision::BLOCK_WALK);

        auto ours = RunOursNone(grid, {.x = 5, .y = 30}, {.x = 55, .y = 30});
        auto ref = RunReferenceNone(grid, {.x = 5, .y = 30}, {.x = 55, .y = 30});
        CheckPathsEquivalent(ours, ref);
    }
}

// ============================================================
// Walk mode (with penalties + reduction)
// Both implementations now have the same penalty model:
//   50 = wide check, 60 = object, 80 = closed door, 10 = 8-neighbor adjacency
// Paths should be cost-equivalent; on unambiguous routes, exact match.
// ============================================================

TEST_SUITE("Reference comparison: walk reduction") {
    TEST_CASE("open grid walk -- exact match") {
        LevelGrid grid({.size = {.width = 100, .height = 100}});
        auto ours = RunOursWalk(grid, {.x = 10, .y = 10}, {.x = 90, .y = 90}, 20);
        auto ref = RunReferenceWalk(grid, {.x = 10, .y = 10}, {.x = 90, .y = 90}, 20);
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("horizontal walk -- exact match") {
        LevelGrid grid({.size = {.width = 100, .height = 100}});
        auto ours = RunOursWalk(grid, {.x = 10, .y = 50}, {.x = 90, .y = 50}, 20);
        auto ref = RunReferenceWalk(grid, {.x = 10, .y = 50}, {.x = 90, .y = 50}, 20);
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsMatch(ours, ref);
    }

    TEST_CASE("walk around wall -- cost equivalent") {
        // Wall paths have ties near the wall where penalties break differently
        // between flat-array and heap-node A* implementations, producing
        // different but equally-optimal detours.
        LevelGrid grid({.size = {.width = 50, .height = 50}});
        for (uint32_t y = 0; y < 40; y++)
            grid.Set({.x = 25, .y = y}, collision::BLOCK_WALK);

        auto ours = RunOursWalk(grid, {.x = 10, .y = 25}, {.x = 40, .y = 25}, 20);
        auto ref = RunReferenceWalk(grid, {.x = 10, .y = 25}, {.x = 40, .y = 25}, 20);
        REQUIRE_FALSE(ours.empty());
        REQUIRE_FALSE(ref.empty());
        CheckPathsEquivalent(ours, ref);
    }
}
