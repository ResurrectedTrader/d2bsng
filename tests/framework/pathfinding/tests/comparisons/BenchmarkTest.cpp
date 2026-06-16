// Benchmark: compare our pathfinder vs the reference implementation.
//
// Both operate on the SAME pre-built LevelGrid (eager loading), so this
// measures algorithmic performance, not data access patterns. In production,
// the reference lazily loads collision from game memory per-point, making it
// even slower than what this benchmark shows.

#include <chrono>
#include <numeric>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"
#include "pathfinding/reference/AStarPath.h"
#include "pathfinding/reference/NoPathReducer.h"
#include "pathfinding/reference/TeleportPathReducer.h"
#include "pathfinding/reference/WalkPathReducer.h"

using d2bs::pathfinding::CollisionLookup;
using d2bs::pathfinding::FindPathOnGrid;
using d2bs::pathfinding::LevelGrid;
using d2bs::pathfinding::Point;
using d2bs::pathfinding::Position;
using d2bs::pathfinding::ReductionType;
using d2bs::pathfinding::collision::BLOCK_WALK;

namespace {

struct BenchResult {
    double oursMs;
    double refMs;
    size_t oursPoints;
    size_t refPoints;
};

BenchResult RunBench(const LevelGrid& grid, Position start, Position end, int32_t iterCount = 10) {
    CollisionLookup coll;
    coll.primary = grid;

    // Warm up
    FindPathOnGrid(coll, start, end, ReductionType::None, 20, {});

    // Benchmark ours
    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<Position> oursResult;
    for (int32_t i = 0; i < iterCount; i++) {
        oursResult = FindPathOnGrid(coll, start, end, ReductionType::None, 20, {});
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double oursMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / iterCount;

    // Benchmark reference - uses signed Point in its own API.
    Point startPt = start.ToPoint();
    Point endPt = end.ToPoint();
    auto map = std::make_shared<TestActMap>(&grid);
    auto reducer = std::make_unique<Mapping::Pathing::Reducing::NoPathReducer>(map);
    Mapping::Pathing::AStarPath<> pather(map, std::move(reducer));
    std::vector<Point> refResult;

    // Warm up
    pather.GetPath(startPt, endPt, refResult);

    auto t2 = std::chrono::high_resolution_clock::now();
    for (int32_t i = 0; i < iterCount; i++) {
        refResult.clear();
        // Need fresh reducer + pather each time since reference has mutable state
        auto r = std::make_unique<Mapping::Pathing::Reducing::NoPathReducer>(map);
        Mapping::Pathing::AStarPath<> p(map, std::move(r));
        p.GetPath(startPt, endPt, refResult);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / iterCount;

    return {.oursMs = oursMs, .refMs = refMs, .oursPoints = oursResult.size(), .refPoints = refResult.size()};
}

}  // namespace

TEST_CASE("Benchmark: 2000x2000 diagonal path") {
    LevelGrid grid({.size = {.width = 2000, .height = 2000}});

    auto result = RunBench(grid, {.x = 100, .y = 100}, {.x = 1900, .y = 1900}, 3);

    MESSAGE("Ours: " << result.oursMs << " ms (" << result.oursPoints << " points)");
    MESSAGE("Ref:  " << result.refMs << " ms (" << result.refPoints << " points)");
    MESSAGE("Speedup: " << result.refMs / result.oursMs << "x");

    CHECK(result.oursPoints > 0);
    CHECK(result.refPoints > 0);
}

TEST_CASE("Benchmark: 2000x2000 with wall forcing detour") {
    LevelGrid grid({.size = {.width = 2000, .height = 2000}});
    for (uint32_t y = 0; y < 1990; y++) {
        grid.Set({.x = 1000, .y = y}, BLOCK_WALK);
    }

    auto result = RunBench(grid, {.x = 500, .y = 1000}, {.x = 1500, .y = 1000}, 3);

    MESSAGE("Ours: " << result.oursMs << " ms (" << result.oursPoints << " points)");
    MESSAGE("Ref:  " << result.refMs << " ms (" << result.refPoints << " points)");
    MESSAGE("Speedup: " << result.refMs / result.oursMs << "x");

    CHECK(result.oursPoints > 0);
    CHECK(result.refPoints > 0);
}

TEST_CASE("Benchmark: 2000x2000 walk reduction") {
    LevelGrid grid({.size = {.width = 2000, .height = 2000}});

    CollisionLookup coll;
    coll.primary = grid;

    constexpr int32_t ITERATIONS = 3;

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<Position> oursResult;
    for (int32_t i = 0; i < ITERATIONS; i++) {
        oursResult = FindPathOnGrid(coll, {.x = 100, .y = 100}, {.x = 1900, .y = 1900}, ReductionType::Walk, 20, {});
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
        p.GetPath({.x = 100, .y = 100}, {.x = 1900, .y = 1900}, refResult);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / ITERATIONS;

    MESSAGE("Ours (walk): " << oursMs << " ms (" << oursResult.size() << " points)");
    MESSAGE("Ref  (walk): " << refMs << " ms (" << refResult.size() << " points)");
    MESSAGE("Speedup: " << refMs / oursMs << "x");

    CHECK(oursResult.size() > 0);
    CHECK(refResult.size() > 0);
}

TEST_CASE("Benchmark: 2000x2000 teleport diagonal") {
    LevelGrid grid({.size = {.width = 2000, .height = 2000}});
    CollisionLookup coll;
    coll.primary = grid;

    constexpr int32_t ITERATIONS = 3;

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<Position> oursResult;
    for (int32_t i = 0; i < ITERATIONS; i++) {
        oursResult =
            FindPathOnGrid(coll, {.x = 100, .y = 100}, {.x = 1900, .y = 1900}, ReductionType::Teleport, 20, {});
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double oursMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / ITERATIONS;

    auto map = std::make_shared<TestActMap>(&grid);
    auto t2 = std::chrono::high_resolution_clock::now();
    std::vector<Point> refResult;
    for (int32_t i = 0; i < ITERATIONS; i++) {
        refResult.clear();
        auto r = std::make_unique<Mapping::Pathing::Reducing::TeleportPathReducer>(map, 20);
        Mapping::Pathing::AStarPath<> p(map, std::move(r));
        p.GetPath({.x = 100, .y = 100}, {.x = 1900, .y = 1900}, refResult);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / ITERATIONS;

    MESSAGE("Ours (tele): " << oursMs << " ms (" << oursResult.size() << " points)");
    MESSAGE("Ref  (tele): " << refMs << " ms (" << refResult.size() << " points)");
    MESSAGE("Speedup: " << refMs / oursMs << "x");

    CHECK(oursResult.size() > 0);
    CHECK(refResult.size() > 0);
}

TEST_CASE("Benchmark: 2000x2000 teleport with wall") {
    LevelGrid grid({.size = {.width = 2000, .height = 2000}});
    for (uint32_t y = 0; y < 1990; y++) {
        grid.Set({.x = 1000, .y = y}, BLOCK_WALK);
    }

    CollisionLookup coll;
    coll.primary = grid;

    constexpr int32_t ITERATIONS = 3;

    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<Position> oursResult;
    for (int32_t i = 0; i < ITERATIONS; i++) {
        oursResult =
            FindPathOnGrid(coll, {.x = 500, .y = 1000}, {.x = 1500, .y = 1000}, ReductionType::Teleport, 20, {});
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double oursMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / ITERATIONS;

    auto map = std::make_shared<TestActMap>(&grid);
    auto t2 = std::chrono::high_resolution_clock::now();
    std::vector<Point> refResult;
    for (int32_t i = 0; i < ITERATIONS; i++) {
        refResult.clear();
        auto r = std::make_unique<Mapping::Pathing::Reducing::TeleportPathReducer>(map, 20);
        Mapping::Pathing::AStarPath<> p(map, std::move(r));
        p.GetPath({.x = 500, .y = 1000}, {.x = 1500, .y = 1000}, refResult);
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double refMs = std::chrono::duration<double, std::milli>(t3 - t2).count() / ITERATIONS;

    MESSAGE("Ours (tele+wall): " << oursMs << " ms (" << oursResult.size() << " points)");
    MESSAGE("Ref  (tele+wall): " << refMs << " ms (" << refResult.size() << " points)");
    MESSAGE("Speedup: " << refMs / oursMs << "x");

    CHECK(oursResult.size() > 0);
    CHECK(refResult.size() > 0);
}
