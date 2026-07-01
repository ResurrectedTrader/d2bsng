#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("Walk mode with penalties avoids wall-adjacent tiles") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 30, .height = 20}});

    // Create a corridor with a wall along y=5 from x=5 to x=25
    // The path from (5,10) to (25,10) should prefer staying away from the wall
    for (uint32_t x = 5; x <= 25; x++) {
        coll.primary.Set({.x = x, .y = 5}, collision::BLOCK_WALK);
    }

    // Walk with penalties (ReductionType::Walk uses penalties)
    auto penaltyPath = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 25, .y = 10}, ReductionType::Walk, 20, {});
    // Walk without penalties
    auto noPenaltyPath = FindPathOnGrid(coll, {.x = 5, .y = 10}, {.x = 25, .y = 10}, ReductionType::None, 20, {});

    CHECK_FALSE(penaltyPath.empty());
    CHECK_FALSE(noPenaltyPath.empty());

    // Count how many points in each path are within 2 tiles of the wall (y <= 7)
    int32_t penaltyNearWall = 0;
    int32_t noPenaltyNearWall = 0;
    for (const auto& pt : penaltyPath) {
        if (pt.y <= 7)
            penaltyNearWall++;
    }
    for (const auto& pt : noPenaltyPath) {
        if (pt.y <= 7)
            noPenaltyNearWall++;
    }

    // Penalty path should have fewer (or equal) wall-adjacent points
    CHECK(penaltyNearWall <= noPenaltyNearWall);
}

TEST_CASE("Object penalty increases path cost near objects") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Place an object in the direct path
    coll.primary.Set({.x = 10, .y = 10}, collision::OBJECT);

    // GetPenalty should return 60 for tiles with object in cross
    CHECK(coll.GetPenalty({.x = 10, .y = 11}) == 60);  // (10, 11) has object at (10, 10) in cross
    CHECK(coll.GetPenalty({.x = 11, .y = 10}) == 60);  // (11, 10) has object at (10, 10) in cross
}

TEST_CASE("Closed door penalty is highest among cross penalties") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Place a closed door
    coll.primary.Set({.x = 10, .y = 10}, collision::CLOSED_DOOR);

    CHECK(coll.GetPenalty({.x = 10, .y = 11}) == 80);
    CHECK(coll.GetPenalty({.x = 10, .y = 9}) == 80);
}

TEST_CASE("No penalty in wide open space") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Check center of empty grid
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 0);
    CHECK(coll.GetPenalty({.x = 5, .y = 5}) == 0);
    CHECK(coll.GetPenalty({.x = 15, .y = 15}) == 0);
}
