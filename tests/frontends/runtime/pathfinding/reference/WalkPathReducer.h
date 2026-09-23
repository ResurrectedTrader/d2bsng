#pragma once

// Adapted from a reference A* pathfinding implementation.
// Changes: replaced ActMap with TestActMap, removed game includes.
// All algorithm logic kept identical. Our implementation matches this
// penalty model (50/60/80 + 8-neighbor scan returning 10).

#include "AStarPath.h"
#include "PathReducer.h"

#include <cstdlib>

namespace Mapping::Pathing::Reducing {

#pragma warning(disable : 4512)

class WalkPathReducer : public PathReducer {
    std::shared_ptr<TestActMap> map;
    int range;

   public:
    WalkPathReducer(const std::shared_ptr<TestActMap>& m, int _range = 20) : map(m), range(_range * 10) {}

    ~WalkPathReducer() override = default;

    void Reduce(const std::vector<Point>& in, std::vector<Point>& out) override {
        if (in.size() < 2) {
            out = in;
            return;
        }

        auto it = in.begin();

        bool init = true;
        int slope = 0;
        Point last;
        Point first = *it;

        for (; *it != in.back(); ++it) {
            Point next = *it;
            const int slope_next = Slope(first, next);
            if (init || slope_next != slope) {
                init = false;
                out.push_back(first);
                last = first;
                slope = slope_next;
            } else if (Euclidean(last, next) >= range) {
                out.push_back(first);
                last = first;
            }
            first = next;
        }

        out.push_back(in.back());
    }

    bool Reject(const Point& pt) override { return checkFlag(map->SpaceGetData(pt)); }

    void GetOpenNodes(const Point& center, std::vector<Point>& out, const Point& /*endpoint*/) override {
        out.push_back({.x = center.x + 1, .y = center.y});
        out.push_back({.x = center.x - 1, .y = center.y});
        out.push_back({.x = center.x, .y = center.y + 1});
        out.push_back({.x = center.x, .y = center.y - 1});
        out.push_back({.x = center.x + 1, .y = center.y + 1});
        out.push_back({.x = center.x - 1, .y = center.y - 1});
        out.push_back({.x = center.x + 1, .y = center.y - 1});
        out.push_back({.x = center.x - 1, .y = center.y + 1});
    }

    int GetPenalty(Point const& pt) override {
        if (checkFlag(map->SpaceGetData(pt, 2))) {
            return 50;
        }

        int data = map->SpaceGetData(pt);

        if ((data & TestActMap::Object) == TestActMap::Object) {
            return 60;
        }

        if ((data & TestActMap::ClosedDoor) == TestActMap::ClosedDoor) {
            return 80;
        }

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0)
                    continue;
                Point adj{.x = pt.x + dx, .y = pt.y + dy};
                int adj_data = map->SpaceGetData(adj);
                if ((adj_data & TestActMap::Object) == TestActMap::Object ||
                    (adj_data & TestActMap::ClosedDoor) == TestActMap::ClosedDoor ||
                    (adj_data & TestActMap::BlockWalk) == TestActMap::BlockWalk) {
                    return 10;
                }
            }
        }
        return 0;
    }

    void MutatePoint(Point& pt) override {
        // find the nearest walkable space
        int area[7][7];

        for (int i = -3; i <= 3; i++) {
            for (int j = -3; j <= 3; j++) {
                if ((i == 0 && j == 0) || abs(i) + abs(j) == 6)
                    continue;
                Point ptN{.x = pt.x + i, .y = pt.y + j};
                area[3 + i][3 + j] = map->GetMapData(ptN);
            }
        }

        for (int i = -2; i <= 2; i++) {
            for (int j = -2; j <= 2; j++) {
                if ((i == 0 && j == 0) || abs(i + j) == 1)
                    continue;

                if (!checkFlag(area[3 + i][3 + j] | area[3 + i + 1][3 + j] | area[3 + i - 1][3 + j] |
                               area[3 + i][3 + j + 1] | area[3 + i][3 + j - 1])) {
                    pt.x += i;
                    pt.y += j;
                    return;
                }
                j++;
            }
        }
    }
};

#pragma warning(default : 4512)

}  // namespace Mapping::Pathing::Reducing
