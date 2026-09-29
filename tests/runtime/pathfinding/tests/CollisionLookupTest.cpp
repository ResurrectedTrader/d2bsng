#include <doctest/doctest.h>

#include "components/navigation/Pathfinder.h"

using namespace d2bs::runtime::navigation;

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
    grid.Set({.x = 100, .y = 200}, CollisionFlag::Wall);
    grid.Set({.x = 105, .y = 205}, CollisionFlag::Object);

    CHECK(grid.Get({.x = 100, .y = 200}) == CollisionFlag::Wall);
    CHECK(grid.Get({.x = 105, .y = 205}) == CollisionFlag::Object);
    CHECK(grid.Get({.x = 101, .y = 200}) == CollisionFlag::None);
    CHECK(grid.Get({.x = 999, .y = 999}) == CollisionFlag::All);  // out of bounds
}

TEST_CASE("CollisionLookup GetCross ORs center and 4 cardinals") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    // Set a single cell and verify GetCross picks it up from neighbors
    coll.primary.Set({.x = 5, .y = 4}, CollisionFlag::Wall);
    // GetCross(5, 5) checks (5,5), (4,5), (6,5), (5,4), (5,6)
    CollisionFlag cross = coll.GetCross({.x = 5, .y = 5});
    CHECK(HasAnyFlag(cross, CollisionFlag::Wall));

    // Clear and check that an isolated cell doesn't affect distant cross
    coll.primary.Set({.x = 5, .y = 4}, CollisionFlag::None);
    cross = coll.GetCross({.x = 5, .y = 5});
    CHECK(cross == CollisionFlag::None);
}

TEST_CASE("IsBlocked checks Wall and NoPlayer in cross") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 10, .height = 10}});

    CHECK_FALSE(coll.IsBlocked({.x = 5, .y = 5}));  // all clear

    // Block a cardinal neighbor
    coll.primary.Set({.x = 5, .y = 4}, CollisionFlag::Wall);  // north of (5,5)
    CHECK(coll.IsBlocked({.x = 5, .y = 5}));

    // Clear walk block, set player block on another neighbor
    coll.primary.Set({.x = 5, .y = 4}, CollisionFlag::None);
    coll.primary.Set({.x = 6, .y = 5}, CollisionFlag::NoPlayer);  // east of (5,5)
    CHECK(coll.IsBlocked({.x = 5, .y = 5}));

    // Object alone doesn't block
    coll.primary.Set({.x = 6, .y = 5}, CollisionFlag::None);
    coll.primary.Set({.x = 5, .y = 5}, CollisionFlag::Object);
    CHECK_FALSE(coll.IsBlocked({.x = 5, .y = 5}));
}

TEST_CASE("GetPenalty returns 50 for wide obstacle, 60 for object, 80 for door") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // No obstacles: penalty is 0
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 0);

    // Place wall at distance 2 (wide penalty = 50)
    coll.primary.Set({.x = 10, .y = 8}, CollisionFlag::Wall);
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 50);

    // Clear wide, place object at distance 1 (cross penalty = 60)
    coll.primary.Set({.x = 10, .y = 8}, CollisionFlag::None);
    coll.primary.Set({.x = 10, .y = 9}, CollisionFlag::Object);
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 60);

    // Clear object, place closed door at distance 1 (cross penalty = 80)
    coll.primary.Set({.x = 10, .y = 9}, CollisionFlag::None);
    coll.primary.Set({.x = 11, .y = 10}, CollisionFlag::Door);
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 80);
}

TEST_CASE("GetPenalty wide has priority over object") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Both wide wall and nearby object
    coll.primary.Set({.x = 10, .y = 8}, CollisionFlag::Wall);    // wide
    coll.primary.Set({.x = 10, .y = 9}, CollisionFlag::Object);  // cross
    // Wide check fires first, returns 50
    CHECK(coll.GetPenalty({.x = 10, .y = 10}) == 50);
}

TEST_CASE("GetWide checks distance-2 cardinals") {
    CollisionLookup coll;
    coll.primary = LevelGrid({.size = {.width = 20, .height = 20}});

    // Place block at distance 2 south: (10, 12) from center (10, 10)
    coll.primary.Set({.x = 10, .y = 12}, CollisionFlag::Wall);
    CollisionFlag wide = coll.GetWide({.x = 10, .y = 10});
    CHECK(HasAnyFlag(wide, CollisionFlag::Wall));

    // Distance 1 should NOT be in wide
    coll.primary.Set({.x = 10, .y = 12}, CollisionFlag::None);
    coll.primary.Set({.x = 10, .y = 11}, CollisionFlag::Wall);  // distance 1
    wide = coll.GetWide({.x = 10, .y = 10});
    // GetWide checks center and dist-2 cardinals only, not dist-1
    // But it also checks center (10,10) which is clear
    CHECK(!HasAnyFlag(wide, CollisionFlag::Wall));
}
