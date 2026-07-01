// Tests for the MapFixture .d2col format: save/load roundtrip,
// FindFixtures directory scanning, and FixtureDir path resolution.

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"
#include "fixtures/MapFixture.h"

using namespace d2bs::pathfinding;
using namespace d2bs::test;

namespace {

MapFixture MakeSyntheticFixture() {
    MapFixture fixture;
    fixture.levelId = 42;
    fixture.grid = LevelGrid({.origin = {.x = 500, .y = 500}, .size = {.width = 200, .height = 200}});

    // Vertical wall at x=600, gap at y=599..601 for cross-check clearance
    for (uint32_t y = fixture.grid.rect.origin.y; y < fixture.grid.rect.origin.y + fixture.grid.rect.size.height; y++) {
        if (y >= 599 && y <= 601)
            continue;
        fixture.grid.Set({.x = 600, .y = y}, collision::BLOCK_WALK);
    }

    return fixture;
}

std::filesystem::path TempDir() {
    return std::filesystem::temp_directory_path() / "d2bs_fixture_test";
}

}  // namespace

TEST_SUITE("MapFixture") {
    TEST_CASE("save and load roundtrip") {
        auto dir = TempDir();
        std::filesystem::create_directories(dir);
        auto filePath = dir / "test_level.d2col";

        auto original = MakeSyntheticFixture();
        REQUIRE(original.Save(filePath));

        auto loaded = MapFixture::Load(filePath);
        REQUIRE(loaded.has_value());

        // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        CHECK(loaded->levelId == original.levelId);
        CHECK(loaded->grid.rect.origin == original.grid.rect.origin);
        CHECK(loaded->grid.rect.size == original.grid.rect.size);
        CHECK(loaded->grid.data.size() == original.grid.data.size());

        for (size_t i = 0; i < original.grid.data.size(); i++) {
            CHECK(loaded->grid.data[i] == original.grid.data[i]);
        }

        CHECK(loaded->grid.Get({.x = 600, .y = 550}) == collision::BLOCK_WALK);
        CHECK(loaded->grid.Get({.x = 600, .y = 600}) == 0);  // gap
        // NOLINTEND(bugprone-unchecked-optional-access)

        std::filesystem::remove_all(dir);
    }

    TEST_CASE("load rejects truncated file") {
        auto dir = TempDir();
        std::filesystem::create_directories(dir);
        auto filePath = dir / "truncated.d2col";

        // Write only 8 bytes (less than 20-byte header)
        std::ofstream file(filePath, std::ios::binary);
        std::array dummy = {0, 0};
        file.write(reinterpret_cast<const char*>(dummy.data()), sizeof(dummy));
        file.close();

        auto loaded = MapFixture::Load(filePath);
        CHECK_FALSE(loaded.has_value());

        std::filesystem::remove_all(dir);
    }

    TEST_CASE("load rejects zero dimensions") {
        auto dir = TempDir();
        std::filesystem::create_directories(dir);
        auto filePath = dir / "zerodim.d2col";

        // Header with width=0
        std::array header = {1, 0, 0, 0, 100};  // levelId=1, w=0, h=100
        std::ofstream file(filePath, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        file.close();

        auto loaded = MapFixture::Load(filePath);
        CHECK_FALSE(loaded.has_value());

        std::filesystem::remove_all(dir);
    }

    TEST_CASE("load returns nullopt for nonexistent file") {
        auto loaded = MapFixture::Load("nonexistent_path_12345.d2col");
        CHECK_FALSE(loaded.has_value());
    }

    TEST_CASE("FindFixtures finds .d2col files") {
        auto dir = TempDir();
        std::filesystem::create_directories(dir);

        auto fixture = MakeSyntheticFixture();
        REQUIRE(fixture.Save(dir / "level_01.d2col"));
        REQUIRE(fixture.Save(dir / "level_02.d2col"));
        std::ofstream(dir / "readme.txt") << "not a fixture";

        auto found = FindFixtures(dir);
        CHECK(found.size() == 2);

        if (found.size() == 2) {
            CHECK(found[0].filename() == "level_01.d2col");
            CHECK(found[1].filename() == "level_02.d2col");
        }

        std::filesystem::remove_all(dir);
    }

    TEST_CASE("FindFixtures returns empty for nonexistent directory") {
        auto found = FindFixtures("nonexistent_dir_12345");
        CHECK(found.empty());
    }

    TEST_CASE("FixtureDir points to maps directory") {
        auto dir = FixtureDir();
        CHECK(dir.filename() == "maps");
        CHECK(dir.parent_path().filename() == "fixtures");
    }

    TEST_CASE("roundtripped grid is pathable") {
        auto dir = TempDir();
        std::filesystem::create_directories(dir);
        auto filePath = dir / "pathable.d2col";

        auto original = MakeSyntheticFixture();
        REQUIRE(original.Save(filePath));

        auto loaded = MapFixture::Load(filePath);
        REQUIRE(loaded.has_value());

        CollisionLookup coll;
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        coll.primary = loaded->grid;

        auto path = FindPathOnGrid(coll, {.x = 550, .y = 600}, {.x = 650, .y = 600}, ReductionType::None, 20, {});
        CHECK_FALSE(path.empty());

        bool crossesWall = false;
        for (const auto& pt : path) {
            if (pt.x >= 600) {
                crossesWall = true;
                break;
            }
        }
        CHECK(crossesWall);

        std::filesystem::remove_all(dir);
    }
}
