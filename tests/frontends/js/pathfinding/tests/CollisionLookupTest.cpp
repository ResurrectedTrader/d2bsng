#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"

using namespace d2bs::pathfinding;

TEST_CASE("LevelGrid Contains checks bounds correctly") {
    LevelGrid grid({.origin = {.x = 100, .y = 200}, .size = {.width = 10, .height = 10}});

    CHECK(grid.Contains({.x = 100, .y = 200}));
    CHECK(grid.Contains({.x = 109, .y = 209}));
    CHECK_FALSE(grid.Contains({.x = 110, .y = 200}));
    CHECK_FALSE(grid.Contains({.x = 99, .y = 200}));
    CHECK_FALSE(grid.Contains({.x = 100, .y = 210}));
    CHECK_FALSE(grid.Contains({.x = 100, .y = 199}));
}

TEST_CASE("LevelGrid Get returns correct values") {
    LevelGrid grid({.origin = {.x = 100, .y = 200}, .size = {.width = 10, .height = 10}});
    grid.Set({.x = 100, .y = 200}, collision::BLOCK_WALK);
    grid.Set({.x = 105, .y = 205}, collision::OBJECT);

    CHECK(grid.Get({.x = 100, .y = 200}) == collision::BLOCK_WALK);
    CHECK(grid.Get({.x = 105, .y = 205}) == collision::OBJECT);
    CHECK(grid.Get({.x = 101, .y = 200}) == 0);
    CHECK(grid.Get({.x = 999, .y = 999}) == collision::AVOID);  // out of bounds
}

TEST_CASE("CollisionLookup GetCross ORs center and 4 cardinals") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    // Set a single cell and verify GetCross picks it up from neighbors
    coll.primary.Set({.x = 5, .y = 4}, collision::BLOCK_WALK);
    // GetCross(5, 5) checks (5,5), (4,5), (6,5), (5,4), (5,6)
    uint16_t cross = coll.GetCross({.x = 5, .y = 5});
    CHECK((cross & collision::BLOCK_WALK) != 0);

    // Clear and check that an isolated cell doesn't affect distant cross
    coll.primary.Set({.x = 5, .y = 4}, 0);
    cross = coll.GetCross({.x = 5, .y = 5});
    CHECK(cross == 0);
}

TEST_CASE("IsBlocked checks BLOCK_WALK and BLOCK_PLAYER in cross") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    CHECK_FALSE(coll.IsBlocked({.x = 5, .y = 5}));  // all clear

    // Block a cardinal neighbor
    coll.primary.Set({.x = 5, .y = 4}, collision::BLOCK_WALK);  // north of (5,5)
    CHECK(coll.IsBlocked({.x = 5, .y = 5}));

    // Clear walk block, set player block on another neighbor
    coll.primary.Set({.x = 5, .y = 4}, 0);
    coll.primary.Set({.x = 6, .y = 5}, collision::BLOCK_PLAYER);  // east of (5,5)
    CHECK(coll.IsBlocked({.x = 5, .y = 5}));

    // Object alone doesn't block
    coll.primary.Set({.x = 6, .y = 5}, 0);
    coll.primary.Set({.x = 5, .y = 5}, collision::OBJECT);
    CHECK_FALSE(coll.IsBlocked({.x = 5, .y = 5}));
}

TEST_CASE("GetPenalty returns 50 for wide obstacle, 60 for object, 80 for door") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // No obstacles: penalty is 0
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 0);

    // Place wall at distance 2 (wide penalty = 50)
    coll.primary.Set({.x = 10, .y = 8}, collision::BLOCK_WALK);
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 50);

    // Clear wide, place object at distance 1 (cross penalty = 60)
    coll.primary.Set({.x = 10, .y = 8}, 0);
    coll.primary.Set({.x = 10, .y = 9}, collision::OBJECT);
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 60);

    // Clear object, place closed door at distance 1 (cross penalty = 80)
    coll.primary.Set({.x = 10, .y = 9}, 0);
    coll.primary.Set({.x = 11, .y = 10}, collision::CLOSED_DOOR);
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 80);
}

TEST_CASE("GetPenalty wide has priority over object") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Both wide wall and nearby object
    coll.primary.Set({.x = 10, .y = 8}, collision::BLOCK_WALK);  // wide
    coll.primary.Set({.x = 10, .y = 9}, collision::OBJECT);      // cross
    // Wide check fires first, returns 50
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 50);
}

TEST_CASE("GetWide checks distance-2 cardinals") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Place block at distance 2 south: (10, 12) from center (10, 10)
    coll.primary.Set({.x = 10, .y = 12}, collision::BLOCK_WALK);
    uint16_t wide = coll.GetWide({.x = 10, .y = 10});
    CHECK((wide & collision::BLOCK_WALK) != 0);

    // Distance 1 should NOT be in wide
    coll.primary.Set({.x = 10, .y = 12}, 0);
    coll.primary.Set({.x = 10, .y = 11}, collision::BLOCK_WALK);  // distance 1
    wide = coll.GetWide({.x = 10, .y = 10});
    // GetWide checks center and dist-2 cardinals only, not dist-1
    // But it also checks center (10,10) which is clear
    CHECK((wide & collision::BLOCK_WALK) == 0);
}
