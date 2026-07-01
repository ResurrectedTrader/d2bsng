#include <cmath>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("Teleport A* finds path in open grid") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 200, .height = 200}});

    auto path = FindPathOnGrid(coll, {.x = 10, .y = 10}, {.x = 190, .y = 190}, ReductionType::Teleport, 20, {});
    CHECK_FALSE(path.empty());
    CHECK(path.front() == (Position{.x = 10, .y = 10}));
    CHECK(path.back() == (Position{.x = 190, .y = 190}));
}

TEST_CASE("Teleport A* jumps are within radius") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 200, .height = 200}});

    auto path = FindPathOnGrid(coll, {.x = 10, .y = 10}, {.x = 190, .y = 190}, ReductionType::Teleport, 20, {});
    CHECK_FALSE(path.empty());

    // Each jump should be within teleport range (radius=20, some tolerance for walk fallback)
    for (size_t i = 1; i < path.size(); i++) {
        double dx = static_cast<int32_t>(path[i].x) - static_cast<int32_t>(path[i - 1].x);
        double dy = static_cast<int32_t>(path[i].y) - static_cast<int32_t>(path[i - 1].y);
        double dist = std::sqrt((dx * dx) + (dy * dy));
        CHECK(dist <= 21.0);
    }
}

TEST_CASE("Teleport A* produces coarse jumps, not tile-by-tile") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 200, .height = 200}});

    int32_t radius = 20;
    auto path = FindPathOnGrid(coll, {.x = 10, .y = 10}, {.x = 190, .y = 10}, ReductionType::Teleport, radius, {});
    CHECK_FALSE(path.empty());

    // Distance is 180 tiles. With radius=20 jumps, expect roughly 180/20 = 9 waypoints.
    // Allow generous range but reject tile-by-tile (which would be ~180 points).
    double distance = 180.0;
    size_t expectedMin = static_cast<size_t>(distance / radius / 2);  // ~4
    size_t expectedMax = static_cast<size_t>(distance / radius * 3);  // ~27
    CHECK(path.size() >= expectedMin);
    CHECK(path.size() <= expectedMax);

    // Verify average jump distance is a significant fraction of the radius
    double totalJumpDist = 0;
    for (size_t i = 1; i < path.size(); i++) {
        double dx = static_cast<int32_t>(path[i].x) - static_cast<int32_t>(path[i - 1].x);
        double dy = static_cast<int32_t>(path[i].y) - static_cast<int32_t>(path[i - 1].y);
        totalJumpDist += std::sqrt((dx * dx) + (dy * dy));
    }
    double avgJump = totalJumpDist / static_cast<double>(path.size() - 1);
    CHECK(avgJump > static_cast<double>(radius) / 2);  // average jump > half the radius
}

TEST_CASE("Teleport A* navigates around wall") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 100, .height = 100}});

    // Vertical wall at x=50, from y=0 to y=90 (gap at y=91..99)
    for (uint32_t y = 0; y < 91; y++) {
        coll.primary.Set({.x = 50, .y = y}, collision::BLOCK_WALK);
    }

    auto path = FindPathOnGrid(coll, {.x = 25, .y = 50}, {.x = 75, .y = 50}, ReductionType::Teleport, 20, {});
    CHECK_FALSE(path.empty());
    CHECK(path.front() == (Position{.x = 25, .y = 50}));
    CHECK(path.back() == (Position{.x = 75, .y = 50}));
}
