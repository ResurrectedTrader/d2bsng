// Real-world benchmarks: run pathfinding on collision grids dumped from the game.
//
// If no .d2col fixture files are found, the test skips gracefully.
// Dump game data to tests/framework/fixtures/maps/ to enable these benchmarks.

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"
#include "fixtures/MapFixture.h"
#include "pathfinding/reference/AStarPath.h"
#include "pathfinding/reference/NoPathReducer.h"
#include "pathfinding/reference/TeleportPathReducer.h"
#include "pathfinding/reference/WalkPathReducer.h"

using namespace d2bs::pathfinding;
using namespace d2bs::test;

namespace {

template <typename T>
double PathDistance(const std::vector<T>& path) {
    double total = 0;
    for (size_t i = 1; i < path.size(); i++) {
        double dx = static_cast<double>(static_cast<int64_t>(path[i].x) - static_cast<int64_t>(path[i - 1].x));
        double dy = static_cast<double>(static_cast<int64_t>(path[i].y) - static_cast<int64_t>(path[i - 1].y));
        total += std::sqrt((dx * dx) + (dy * dy));
    }
    return total;
}

}  // namespace

TEST_CASE("Real world benchmarks") {
    auto fixtureFiles = FindFixtures(FixtureDir());
    if (fixtureFiles.empty()) {
        MESSAGE("No .d2col fixture files found in " << FixtureDir().string());
        MESSAGE("Dump game data to tests/framework/fixtures/maps/ to enable real-world benchmarks");
        return;  // Skip gracefully
    }

    for (const auto& file : fixtureFiles) {
        auto fixture = MapFixture::Load(file);
        REQUIRE(fixture.has_value());

        SUBCASE(file.stem().string().c_str()) {
            auto& grid = fixture->grid;
            MESSAGE("Level " << fixture->levelId << " (" << grid.rect.size.width << "x" << grid.rect.size.height
                             << ")");

            // Find walkable start and end points.
            // Start: first walkable tile from top-left.
            // End: first walkable tile from bottom-right.
            CollisionLookup coll;
            coll.primary = grid;

            Point start = Point::Zero;
            Point end = Point::Zero;
            bool foundStart = false;
            bool foundEnd = false;

            auto minX = static_cast<int32_t>(grid.rect.origin.x) + 5;
            auto minY = static_cast<int32_t>(grid.rect.origin.y) + 5;
            auto maxX = static_cast<int32_t>(grid.rect.origin.x + grid.rect.size.width) - 5;
            auto maxY = static_cast<int32_t>(grid.rect.origin.y + grid.rect.size.height) - 5;

            // Search for walkable start from top-left.
            for (int32_t y = minY; y < maxY && !foundStart; y++) {
                for (int32_t x = minX; x < maxX && !foundStart; x++) {
                    if (!coll.IsBlocked({.x = x, .y = y})) {
                        start = {.x = x, .y = y};
                        foundStart = true;
                    }
                }
            }

            // Search for walkable end from bottom-right.
            for (int32_t y = maxY - 1; y >= minY && !foundEnd; y--) {
                for (int32_t x = maxX - 1; x >= minX && !foundEnd; x--) {
                    if (!coll.IsBlocked({.x = x, .y = y})) {
                        end = {.x = x, .y = y};
                        foundEnd = true;
                    }
                }
            }

            if (!foundStart || !foundEnd) {
                // Fixture has no walkable cells - likely a stale/empty dump. Skip.
                MESSAGE("Skipping - no walkable start/end (re-dump fixture to enable)");
                continue;
            }
            MESSAGE("Start: (" << start.x << "," << start.y << ") End: (" << end.x << "," << end.y << ")");

            Position startPos = start.ToPosition();
            Position endPos = end.ToPosition();

            constexpr int32_t ITERATIONS = 5;

            // --- Walk mode benchmark ---
            {
                auto t0 = std::chrono::high_resolution_clock::now();
                std::vector<Position> result;
                for (int32_t i = 0; i < ITERATIONS; i++) {
                    result = FindPathOnGrid(coll, startPos, endPos, ReductionType::Walk, 20, {});
                }
                auto t1 = std::chrono::high_resolution_clock::now();
                double oursMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / ITERATIONS;

                auto map = std::make_shared<TestActMap>(&grid);
                auto t2 = std::chrono::high_resolution_clock::now();
                std::vector<Point> refResult;
                for (int32_t i = 0; i < ITERATIONS; i++) {
                    refResult.clear();
                    auto r = std::make_unique<Mapping::Pathing::Reducing::WalkPathReducer>(map, 20);
                    Mapping::Pathing::AStarPath<> p(map, std::move(r));
                    p.GetPath(start, end, refResult);
                }
                auto t3 = std::chrono::high_resolution_clock::now();
                double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / ITERATIONS;

                MESSAGE("  Walk: ours=" << oursMs << "ms (" << result.size() << "pts, d=" << PathDistance(result)
                                        << "), ref=" << refMs << "ms (" << refResult.size() << "pts, d="
                                        << PathDistance(refResult) << "), speedup=" << refMs / oursMs << "x");
            }

            // --- Teleport mode benchmark - sweep hWeight ---
            {
                auto map = std::make_shared<TestActMap>(&grid);
                auto t2 = std::chrono::high_resolution_clock::now();
                std::vector<Point> refResult;
                for (int32_t i = 0; i < ITERATIONS; i++) {
                    refResult.clear();
                    auto r = std::make_unique<Mapping::Pathing::Reducing::TeleportPathReducer>(map, 20);
                    Mapping::Pathing::AStarPath<> p(map, std::move(r));
                    p.GetPath(start, end, refResult);
                }
                auto t3 = std::chrono::high_resolution_clock::now();
                double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / ITERATIONS;
                double refDist = PathDistance(refResult);

                MESSAGE("  Teleport ref: " << refMs << "ms (" << refResult.size() << "pts, d=" << refDist << ")");

                std::array<double, 3> weights = {1.0, 1.5, 3.0};
                for (double w : weights) {
                    auto t0 = std::chrono::high_resolution_clock::now();
                    std::vector<Position> result;
                    for (int32_t i = 0; i < ITERATIONS; i++) {
                        result = FindPathOnGrid(coll, startPos, endPos, ReductionType::Teleport, 20, {}, nullptr,
                                                nullptr, nullptr, w);
                    }
                    auto t1 = std::chrono::high_resolution_clock::now();
                    double oursMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / ITERATIONS;

                    MESSAGE("  Teleport w=" << w << ": ours=" << oursMs << "ms (" << result.size() << "pts, d="
                                            << PathDistance(result) << "), speedup=" << refMs / oursMs << "x");
                }
            }

            // --- None/raw mode benchmark ---
            {
                auto t0 = std::chrono::high_resolution_clock::now();
                std::vector<Position> result;
                for (int32_t i = 0; i < ITERATIONS; i++) {
                    result = FindPathOnGrid(coll, startPos, endPos, ReductionType::None, 20, {});
                }
                auto t1 = std::chrono::high_resolution_clock::now();
                double oursMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / ITERATIONS;

                auto map = std::make_shared<TestActMap>(&grid);
                auto t2 = std::chrono::high_resolution_clock::now();
                std::vector<Point> refResult;
                for (int32_t i = 0; i < ITERATIONS; i++) {
                    refResult.clear();
                    auto r = std::make_unique<Mapping::Pathing::Reducing::NoPathReducer>(map);
                    Mapping::Pathing::AStarPath<> p(map, std::move(r));
                    p.GetPath(start, end, refResult);
                }
                auto t3 = std::chrono::high_resolution_clock::now();
                double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / ITERATIONS;

                MESSAGE("  Raw: ours=" << oursMs << "ms (" << result.size() << "pts, d=" << PathDistance(result)
                                       << "), ref=" << refMs << "ms (" << refResult.size() << "pts, d="
                                       << PathDistance(refResult) << "), speedup=" << refMs / oursMs << "x");
            }
        }
    }
}
