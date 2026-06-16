#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("Walk reduction on straight horizontal line") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 110, .height = 20}});

    // Use interior points to avoid edge mutation (cross check hits AVOID at boundary)
    auto raw = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 104, .y = 10}, ReductionType::None, 20, {});
    auto reduced = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 104, .y = 10}, ReductionType::Walk, 20, {});

    CHECK_FALSE(raw.empty());
    CHECK_FALSE(reduced.empty());
    CHECK(raw.size() == 100);
    // Reduced should be significantly fewer points on a straight line
    CHECK(reduced.size() < raw.size());
    CHECK(reduced.front() == (Position{.x = 5, .y = 10}));
    CHECK(reduced.back() == (Position{.x = 104, .y = 10}));
}

TEST_CASE("Walk reduction on diagonal line") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 60, .height = 60}});

    // Use interior points to avoid edge mutation
    auto raw = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 50, .y = 50}, ReductionType::None, 20, {});
    auto reduced = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 50, .y = 50}, ReductionType::Walk, 20, {});

    CHECK_FALSE(raw.empty());
    CHECK_FALSE(reduced.empty());
    CHECK(reduced.size() < raw.size());
    CHECK(reduced.front() == (Position{.x = 5, .y = 5}));
    CHECK(reduced.back() == (Position{.x = 50, .y = 50}));
}

TEST_CASE("Walk reduction preserves endpoints around corner") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 30, .height = 30}});

    // L-shaped wall forcing a corner path
    for (uint32_t x = 10; x < 20; x++) {
        coll.primary.Set({.x = x, .y = 15}, collision::BLOCK_WALK);
    }

    auto reduced = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 25, .y = 20}, ReductionType::Walk, 20, {});
    CHECK_FALSE(reduced.empty());
    CHECK(reduced.front() == (Position{.x = 5, .y = 10}));
    CHECK(reduced.back() == (Position{.x = 25, .y = 20}));
}

TEST_CASE("Walk reduction on 2-point path returns both points") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    // Adjacent points (1 step apart)
    auto path = FindPathOnGrid(coll, {.x = 3, .y = 3}, {.x = 4, .y = 3}, ReductionType::Walk, 20, {});
    CHECK_FALSE(path.empty());
    CHECK(path.front() == (Position{.x = 3, .y = 3}));
    CHECK(path.back() == (Position{.x = 4, .y = 3}));
}
