#include "fixtures/MapFixture.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace d2bs::test {

namespace {

// On-disk header format dumped by d2bs's collision tool (`tools/dump_collision.js`).
// Fixed 20-byte layout: 5 × uint32_t little-endian in this order:
//
//   offset 0:  levelId       (uint32)
//   offset 4:  origin.x      (uint32, game-coords)
//   offset 8:  origin.y      (uint32, game-coords)
//   offset 12: size.width    (uint32, game-coords)
//   offset 16: size.height   (uint32, game-coords)
//   offset 20: grid data (size.width × size.height cells × int32)
//
// `Rect` is `{Position origin; Size size;}` and both Position / Size are
// `{uint32_t, uint32_t}` aggregates with standard layout, so the in-memory
// layout of `{levelId, Rect rect}` is byte-identical to the on-disk stream
// above - a single `fread(&header, 20)` populates it correctly, and the
// static_assert below locks that invariant in.
//
// The dump tool wrote the grid-coord fields as SIGNED int32 (C's default int),
// but every valid value is non-negative, so the bit pattern is identical to
// uint32 and reads correctly into the unsigned aggregates.
struct D2ColHeader {
    uint32_t levelId;
    game::Rect rect;
};

static_assert(sizeof(D2ColHeader) == 20);
static_assert(std::is_trivially_copyable_v<D2ColHeader>);

}  // namespace

std::optional<MapFixture> MapFixture::Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;

    D2ColHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good())
        return std::nullopt;
    if (header.rect.size.width == 0 || header.rect.size.height == 0)
        return std::nullopt;

    auto cellCount = header.rect.size.Area();

    // Read int32 per cell, mask to uint16
    std::vector<int32_t> raw(cellCount);
    file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(cellCount) * sizeof(int32_t));
    if (!file.good())
        return std::nullopt;

    std::vector<uint16_t> data(cellCount);
    for (size_t i = 0; i < cellCount; i++) {
        data[i] = static_cast<uint16_t>(raw[i] & 0xFFFF);
    }

    MapFixture fixture;
    fixture.levelId = header.levelId;
    fixture.grid = pathfinding::LevelGrid(header.rect);
    fixture.grid.data = std::move(data);
    return fixture;
}

bool MapFixture::Save(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;

    D2ColHeader header{
        .levelId = levelId,
        .rect = grid.rect,
    };

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write as int32 per cell (matching d2bs binary dump format)
    auto cellCount = grid.rect.size.Area();
    for (size_t i = 0; i < cellCount; i++) {
        auto val = static_cast<int32_t>(grid.data[i]);
        file.write(reinterpret_cast<const char*>(&val), sizeof(val));
    }
    file.flush();
    return file.good();
}

std::vector<std::filesystem::path> FindFixtures(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> results;
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
        return results;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".d2col") {
            results.push_back(entry.path());
        }
    }

    std::ranges::sort(results);
    return results;
}

std::filesystem::path FixtureDir() {
    std::array candidates = {
        std::filesystem::path(__FILE__).parent_path() / "maps",
        std::filesystem::path("tests/frontends/js/fixtures/maps"),
        std::filesystem::path("../tests/frontends/js/fixtures/maps"),
    };
    for (auto& p : candidates) {
        if (std::filesystem::exists(p))
            return p;
    }
    return candidates[0];
}

}  // namespace d2bs::test
