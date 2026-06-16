#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("Walk A* finds path in open grid") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 50, .height = 50}});

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 45, .y = 45}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());
    CHECK(path.front() == (Position{.x = 5, .y = 5}));
    CHECK(path.back() == (Position{.x = 45, .y = 45}));
}

TEST_CASE("Walk A* navigates around vertical wall") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Vertical wall at x=10 from y=0 to y=15 (with cross-blocked neighbors)
    for (uint32_t y = 0; y < 16; y++) {
        coll.primary.Set({.x = 10, .y = y}, collision::BLOCK_WALK);
    }

    auto path = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 15, .y = 10}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());
    CHECK(path.front() == (Position{.x = 5, .y = 10}));
    CHECK(path.back() == (Position{.x = 15, .y = 10}));

    // Verify path doesn't cross the wall
    for (const auto& pt : path) {
        CHECK_FALSE((pt.x == 10 && pt.y < 16));
    }
}

TEST_CASE("Walk A* returns contiguous steps") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    auto path = FindPathOnGrid(coll, {.x = 2, .y = 2}, {.x = 18, .y = 18}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());

    // Each step should move at most 1 tile in each direction (8-connected)
    for (size_t i = 1; i < path.size(); i++) {
        int32_t dx = std::abs(static_cast<int32_t>(path[i].x) - static_cast<int32_t>(path[i - 1].x));
        int32_t dy = std::abs(static_cast<int32_t>(path[i].y) - static_cast<int32_t>(path[i - 1].y));
        CHECK(dx <= 1);
        CHECK(dy <= 1);
        CHECK((dx + dy) > 0);  // must move
    }
}

TEST_CASE("Walk A* with reduction produces fewer points") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 50, .height = 50}});

    auto raw = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 45, .y = 5}, ReductionType::None, 20, {});
    auto reduced = FindPathOnGrid(coll, {.x = 5, .y = 5}, {.x = 45, .y = 5}, ReductionType::Walk, 20, {});
    CHECK_FALSE(raw.empty());
    CHECK_FALSE(reduced.empty());
    CHECK(reduced.size() < raw.size());
    CHECK(reduced.front() == (Position{.x = 5, .y = 5}));
    CHECK(reduced.back() == (Position{.x = 45, .y = 5}));
}

TEST_CASE("Walk A* straight line produces optimal length") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 50, .height = 20}});

    // Use interior points to avoid edge mutation (cross check hits AVOID at grid boundary)
    auto path = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 45, .y = 10}, ReductionType::None, 20, {});
    CHECK_FALSE(path.empty());
    // Horizontal path should be exactly 41 tiles (5..45 inclusive)
    CHECK(path.size() == 41);
}
