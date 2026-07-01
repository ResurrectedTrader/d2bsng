#include <stop_token>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("start equals end returns single-point path") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 5, .y = 5}, ReductionType::Walk, 20, {});
    CHECK(path.size() == 1);
    CHECK(path[0] == (Position{.x = 5, .y = 5}));
}

TEST_CASE("unreachable destination returns empty path") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 10}});

    // Complete wall dividing the grid vertically at x=10
    for (uint32_t y = 0; y < 10; y++) {
        coll.primary.Set({.x = 10, .y = y}, collision::BLOCK_WALK);
    }

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 15, .y = 5}, ReductionType::None, 20, {});
    CHECK(path.empty());
}

TEST_CASE("cancellation via stop_token returns empty") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 1000, .height = 1000}});

    // Block most of the grid to force A* to explore many nodes (>256 iterations)
    for (uint32_t y = 2; y < 999; y++) {
        coll.primary.Set({.x = 500, .y = y}, collision::BLOCK_WALK);
    }

    std::stop_source src;
    src.request_stop();  // pre-cancelled
    auto path =
        FindPathOnGrid(coll, {.x = 100, .y = 500}, {.x = 900, .y = 500}, ReductionType::None, 20, src.get_token());
    CHECK(path.empty());
}

TEST_CASE("empty grid returns empty path") {
    CollisionLookup coll;
    // primary has zero dimensions
    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 10, .y = 10}, ReductionType::None, 20, {});
    CHECK(path.empty());
}

TEST_CASE("start out of bounds returns empty path") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    auto path = FindPathOnGrid(coll, {.x = 99999, .y = 99999}, {.x = 5, .y = 5}, ReductionType::None, 20, {});
    CHECK(path.empty());
}

TEST_CASE("end out of bounds returns empty path") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 50, .y = 50}, ReductionType::None, 20, {});
    CHECK(path.empty());
}

TEST_CASE("adjacent start and end") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 6, .y = 5}, ReductionType::None, 20, {});
    CHECK(path.size() == 2);
    CHECK(path[0] == (Position{.x = 5, .y = 5}));
    CHECK(path[1] == (Position{.x = 6, .y = 5}));
}

TEST_CASE("diagonal adjacent start and end") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 6, .y = 6}, ReductionType::None, 20, {});
    CHECK(path.size() == 2);
    CHECK(path[0] == (Position{.x = 5, .y = 5}));
    CHECK(path[1] == (Position{.x = 6, .y = 6}));
}

TEST_CASE("non-zero origin grid works correctly") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.origin = {.x = 1000, .y = 2000}, .size = {.width = 50, .height = 50}});

    auto path = FindPathOnGrid(coll, {.x = 1005, .y = 2005}, {.x = 1045, .y = 2045}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());
    CHECK(path.front() == (Position{.x = 1005, .y = 2005}));
    CHECK(path.back() == (Position{.x = 1045, .y = 2045}));
}
