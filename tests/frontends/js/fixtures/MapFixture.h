#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "components/pathfinding/Pathfinder.h"

namespace d2bs::test {

// .d2col format (raw d2bs dump, all 4-byte LE values):
//   uint32 levelId
//   uint32 origin.x, origin.y      (Position)
//   uint32 size.width, size.height (Size)
//   int32[width*height] collision  (uint16 values stored as int32)

struct MapFixture {
    uint32_t levelId = 0;
    d2bs::pathfinding::LevelGrid grid;

    static std::optional<MapFixture> Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
};

std::vector<std::filesystem::path> FindFixtures(const std::filesystem::path& dir);

// Standard fixture directory: tests/frontends/js/fixtures/maps/
std::filesystem::path FixtureDir();

}  // namespace d2bs::test
