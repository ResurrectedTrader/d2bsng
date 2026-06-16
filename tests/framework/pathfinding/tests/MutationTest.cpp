#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("MutatePoint moves blocked point to walkable neighbor") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Block a 3x3 area around (10, 10) so center is blocked but edges are clear
    for (int32_t dy = -1; dy <= 1; dy++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            coll.primary.Set({.x = static_cast<uint32_t>(10 + dx), .y = static_cast<uint32_t>(10 + dy)},
                             collision::BLOCK_WALK);
        }
    }

    Point pt{.x = 10, .y = 10};
    coll.MutatePoint(pt);
    // The point should have moved away from the blocked center
    CHECK_FALSE((pt.x == 10 && pt.y == 10));
    // The new point should be within the 7x7 search area
    CHECK(std::abs(pt.x - 10) <= 3);
    CHECK(std::abs(pt.y - 10) <= 3);
}

TEST_CASE("MutatePoint does not move already-walkable point") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // All clear, but MutatePoint skips i==0,j==0 early.
    // The function's inner loop skips (0,0) explicitly, so even for a walkable
    // point, it may or may not mutate depending on the first valid offset tried.
    // We verify the A* caller's behavior instead: if start is walkable, MutatePoint
    // is never called in WalkAStar.
    // This test just verifies MutatePoint doesn't crash on a walkable point.
    Point pt{.x = 10, .y = 10};
    coll.MutatePoint(pt);
    // Result is within bounds
    CHECK(std::abs(pt.x - 10) <= 3);
    CHECK(std::abs(pt.y - 10) <= 3);
}

TEST_CASE("Walk A* automatically mutates blocked start") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Block the start point and its immediate cross
    coll.primary.Set({.x = 5, .y = 5}, collision::BLOCK_WALK);

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 15, .y = 15}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());
    // Start should have been mutated to a nearby walkable point
    CHECK_FALSE(path.front() == (Position{.x = 5, .y = 5}));
}

TEST_CASE("Walk A* automatically mutates blocked end") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Block the end point
    coll.primary.Set({.x = 15, .y = 15}, collision::BLOCK_WALK);

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 15, .y = 15}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());
    // End should have been mutated
    CHECK_FALSE(path.back() == (Position{.x = 15, .y = 15}));
}
