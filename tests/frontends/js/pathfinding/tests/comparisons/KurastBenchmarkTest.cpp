// Benchmarks on real Kurast collision data (Act 3):
// 1. Short walk: 5 cells within Flayer Jungle (78)
// 2. Long walk: corner to corner within Flayer Jungle (78)
// 3. Cross-level: Flayer Jungle (78) -> Travincal (83), 6 levels
// All with walk+reduce and teleport modes, compared against reference.

#include <array>
#include <chrono>
#include <utility>

#include <doctest/doctest.h>

#include "components/pathfinding/Pathfinder.h"
#include "fixtures/MapFixture.h"
#include "pathfinding/reference/AStarPath.h"
#include "pathfinding/reference/TeleportPathReducer.h"
#include "pathfinding/reference/WalkPathReducer.h"

using namespace d2bs::pathfinding;
using namespace d2bs::test;

namespace {

// Load a .d2col fixture, skip test if not found
std::optional<MapFixture> LoadRequired(const char* filename) {
    auto path = FixtureDir() / filename;
    return MapFixture::Load(path);
}

// Find a walkable point in `grid`, starting at `start` and scanning by `step`
// until a non-blocked cell is hit or the grid bounds (minus a 5-cell margin) are
// exhausted. Returns (0,0) on failure.
Point FindWalkable(CollisionLookup& coll, const LevelGrid& grid, Point start, Point step) {
    auto minX = static_cast<int32_t>(grid.rect.origin.x) + 5;
    auto minY = static_cast<int32_t>(grid.rect.origin.y) + 5;
    auto maxX = static_cast<int32_t>(grid.rect.origin.x + grid.rect.size.width) - 5;
    auto maxY = static_cast<int32_t>(grid.rect.origin.y + grid.rect.size.height) - 5;
    for (int32_t y = start.y; y >= minY && y < maxY; y += step.y) {
        for (int32_t x = start.x; x >= minX && x < maxX; x += step.x) {
            if (!coll.IsBlocked({.x = x, .y = y}))
                return {.x = x, .y = y};
        }
    }
    return Point::Zero;
}

struct BenchStats {
    double min;
    double max;
    double avg;
};

template <typename Fn>
BenchStats Bench(const Fn& fn, int32_t runs) {
    double total = 0;
    double lo = 1e9;
    double hi = 0;
    for (int32_t i = 0; i < runs; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total += ms;
        lo = std::min(lo, ms);
        hi = std::max(hi, ms);
    }
    return {.min = lo, .max = hi, .avg = total / runs};
}

void PrintBench(const char* mode, BenchStats ours, size_t oursPts, BenchStats ref, size_t refPts) {
    MESSAGE("  " << mode << ": ours=" << ours.avg << "ms [" << ours.min << "-" << ours.max << "] (" << oursPts
                 << "pts), ref=" << ref.avg << "ms [" << ref.min << "-" << ref.max << "] (" << refPts
                 << "pts), speedup=" << ref.avg / ours.avg << "x");
}

void CheckEndpoints(const std::vector<Position>& ours, const std::vector<Point>& ref, Point expectedStart,
                    Point expectedEnd) {
    REQUIRE_FALSE(ours.empty());
    REQUIRE_FALSE(ref.empty());
    CHECK(ours.front().ToPoint() == expectedStart);
    CHECK(ours.back().ToPoint() == expectedEnd);
    CHECK(ref.front() == expectedStart);
    CHECK(ref.back() == expectedEnd);
}

}  // namespace

TEST_CASE("Kurast: short walk in Flayer Jungle (5 cells)") {
    auto fixture = LoadRequired("level_078_flayer_jungle.d2col");
    if (!fixture) {
        MESSAGE("Skipping - fixture not found");
        return;
    }

    CollisionLookup coll;
    coll.primary = fixture->grid;

    Point start =
        FindWalkable(coll, fixture->grid,
                     {.x = static_cast<int32_t>(fixture->grid.rect.origin.x + (fixture->grid.rect.size.width / 2)),
                      .y = static_cast<int32_t>(fixture->grid.rect.origin.y + (fixture->grid.rect.size.height / 2))},
                     {.x = 1, .y = 1});
    REQUIRE(start.x != 0);
    Point end = {.x = start.x + 5, .y = start.y};
    if (coll.IsBlocked(end))
        end = FindWalkable(coll, fixture->grid, {.x = start.x + 5, .y = start.y}, {.x = 1, .y = 1});
    REQUIRE(end.x != 0);
    MESSAGE("Start: (" << start.x << "," << start.y << ") End: (" << end.x << "," << end.y << ")");

    Position startPos = start.ToPosition();
    Position endPos = end.ToPosition();

    constexpr int32_t RUNS = 10;
    auto map = std::make_shared<TestActMap>(&fixture->grid);

    std::vector<Position> walkResult;
    std::vector<Point> refWalkResult;
    std::vector<Position> teleResult;
    std::vector<Point> refTeleResult;
    auto oursWalk =
        Bench([&] { walkResult = FindPathOnGrid(coll, startPos, endPos, ReductionType::Walk, 20, {}); }, RUNS);
    auto oursTele =
        Bench([&] { teleResult = FindPathOnGrid(coll, startPos, endPos, ReductionType::Teleport, 20, {}); }, RUNS);
    auto rWalk = Bench(
        [&] {
            refWalkResult.clear();
            auto r = std::make_unique<Mapping::Pathing::Reducing::WalkPathReducer>(map, 20);
            Mapping::Pathing::AStarPath p(map, std::move(r));
            p.GetPath(start, end, refWalkResult);
        },
        RUNS);
    auto rTele = Bench(
        [&] {
            refTeleResult.clear();
            auto r = std::make_unique<Mapping::Pathing::Reducing::TeleportPathReducer>(map, 20);
            Mapping::Pathing::AStarPath p(map, std::move(r));
            p.GetPath(start, end, refTeleResult);
        },
        RUNS);

    PrintBench("Walk", oursWalk, walkResult.size(), rWalk, refWalkResult.size());
    PrintBench("Tele", oursTele, teleResult.size(), rTele, refTeleResult.size());
    CheckEndpoints(walkResult, refWalkResult, start, end);
    CheckEndpoints(teleResult, refTeleResult, start, end);
}

TEST_CASE("Kurast: long walk within Flayer Jungle (corner to corner)") {
    auto fixture = LoadRequired("level_078_flayer_jungle.d2col");
    if (!fixture) {
        MESSAGE("Skipping - fixture not found");
        return;
    }

    CollisionLookup coll;
    coll.primary = fixture->grid;

    Point start = FindWalkable(coll, fixture->grid,
                               {.x = static_cast<int32_t>(fixture->grid.rect.origin.x + 10),
                                .y = static_cast<int32_t>(fixture->grid.rect.origin.y + 10)},
                               {.x = 1, .y = 1});
    Point end =
        FindWalkable(coll, fixture->grid,
                     {.x = static_cast<int32_t>(fixture->grid.rect.origin.x + fixture->grid.rect.size.width - 10),
                      .y = static_cast<int32_t>(fixture->grid.rect.origin.y + fixture->grid.rect.size.height - 10)},
                     {.x = -1, .y = -1});
    REQUIRE(start.x != 0);
    REQUIRE(end.x != 0);
    MESSAGE("Start: (" << start.x << "," << start.y << ") End: (" << end.x << "," << end.y << ")");

    Position startPos = start.ToPosition();
    Position endPos = end.ToPosition();

    constexpr int32_t RUNS = 5;
    auto map = std::make_shared<TestActMap>(&fixture->grid);

    std::vector<Position> walkResult;
    std::vector<Point> refWalkResult;
    std::vector<Position> teleResult;
    std::vector<Point> refTeleResult;
    auto oursWalk =
        Bench([&] { walkResult = FindPathOnGrid(coll, startPos, endPos, ReductionType::Walk, 20, {}); }, RUNS);
    auto oursTele =
        Bench([&] { teleResult = FindPathOnGrid(coll, startPos, endPos, ReductionType::Teleport, 20, {}); }, RUNS);
    auto rWalk = Bench(
        [&] {
            refWalkResult.clear();
            auto r = std::make_unique<Mapping::Pathing::Reducing::WalkPathReducer>(map, 20);
            Mapping::Pathing::AStarPath p(map, std::move(r));
            p.GetPath(start, end, refWalkResult);
        },
        RUNS);
    auto rTele = Bench(
        [&] {
            refTeleResult.clear();
            auto r = std::make_unique<Mapping::Pathing::Reducing::TeleportPathReducer>(map, 20);
            Mapping::Pathing::AStarPath p(map, std::move(r));
            p.GetPath(start, end, refTeleResult);
        },
        RUNS);

    PrintBench("Walk", oursWalk, walkResult.size(), rWalk, refWalkResult.size());
    PrintBench("Tele", oursTele, teleResult.size(), rTele, refTeleResult.size());
    CheckEndpoints(walkResult, refWalkResult, start, end);
    CheckEndpoints(teleResult, refTeleResult, start, end);
}

TEST_CASE("Kurast: cross-level Flayer Jungle(78) to Travincal(83)") {
    const std::array files = {
        "level_078_flayer_jungle.d2col", "level_079_lower_kurast.d2col",    "level_080_kurast_bazaar.d2col",
        "level_081_upper_kurast.d2col",  "level_082_kurast_causeway.d2col", "level_083_travincal.d2col",
    };

    std::vector<MapFixture> fixtures;
    fixtures.reserve(std::size(files));
    for (const auto* f : files) {
        auto fix = LoadRequired(f);
        if (!fix) {
            MESSAGE("Skipping - " << f << " not found");
            return;
        }
        fixtures.push_back(std::move(*fix));
    }

    CollisionLookup coll;
    coll.primary = fixtures[0].grid;
    for (size_t i = 1; i < fixtures.size(); i++)
        coll.secondary[fixtures[i].levelId] = fixtures[i].grid;

    Point start = FindWalkable(
        coll, fixtures[0].grid,
        {.x = static_cast<int32_t>(fixtures[0].grid.rect.origin.x + (fixtures[0].grid.rect.size.width / 2)),
         .y = static_cast<int32_t>(fixtures[0].grid.rect.origin.y + (fixtures[0].grid.rect.size.height / 2))},
        {.x = 1, .y = 1});
    Point end = FindWalkable(
        coll, fixtures.back().grid,
        {.x = static_cast<int32_t>(fixtures.back().grid.rect.origin.x + (fixtures.back().grid.rect.size.width / 2)),
         .y = static_cast<int32_t>(fixtures.back().grid.rect.origin.y + (fixtures.back().grid.rect.size.height / 2))},
        {.x = 1, .y = 1});
    REQUIRE(start.x != 0);
    REQUIRE(end.x != 0);
    MESSAGE("Start (Flayer Jungle): (" << start.x << "," << start.y << ")");
    MESSAGE("End (Travincal):       (" << end.x << "," << end.y << ")");

    Position startPos = start.ToPosition();
    Position endPos = end.ToPosition();

    std::vector<const LevelGrid*> allGrids;
    allGrids.reserve(fixtures.size());
    for (auto& f : fixtures)
        allGrids.push_back(&f.grid);
    auto map = std::make_shared<TestActMap>(allGrids);

    constexpr int32_t RUNS = 5;
    std::vector<Position> walkResult;
    std::vector<Point> refWalkResult;
    std::vector<Position> teleResult;
    std::vector<Point> refTeleResult;
    auto oursWalk =
        Bench([&] { walkResult = FindPathOnGrid(coll, startPos, endPos, ReductionType::Walk, 20, {}); }, RUNS);
    auto oursTele =
        Bench([&] { teleResult = FindPathOnGrid(coll, startPos, endPos, ReductionType::Teleport, 20, {}); }, RUNS);
    auto rWalk = Bench(
        [&] {
            refWalkResult.clear();
            auto r = std::make_unique<Mapping::Pathing::Reducing::WalkPathReducer>(map, 20);
            Mapping::Pathing::AStarPath p(map, std::move(r));
            p.GetPath(start, end, refWalkResult);
        },
        RUNS);
    auto rTele = Bench(
        [&] {
            refTeleResult.clear();
            auto r = std::make_unique<Mapping::Pathing::Reducing::TeleportPathReducer>(map, 20);
            Mapping::Pathing::AStarPath p(map, std::move(r));
            p.GetPath(start, end, refTeleResult);
        },
        RUNS);

    PrintBench("Walk", oursWalk, walkResult.size(), rWalk, refWalkResult.size());
    PrintBench("Tele", oursTele, teleResult.size(), rTele, refTeleResult.size());
    CheckEndpoints(walkResult, refWalkResult, start, end);
    CheckEndpoints(teleResult, refTeleResult, start, end);
}
