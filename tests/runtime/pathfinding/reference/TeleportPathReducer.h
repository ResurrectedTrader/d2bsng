#pragma once

// Adapted from a reference A* pathfinding implementation.
// Changes: replaced ActMap with TestActMap, removed game includes.
// All algorithm logic kept identical.

#include "AStarPath.h"
#include "PathReducer.h"

#include <cstdlib>

namespace Mapping::Pathing::Reducing {

#pragma warning(disable : 4512)

class TeleportPathReducer : public PathReducer {
    std::shared_ptr<TestActMap> map;
    int range;
    bool justExpand;
    std::vector<Point> distanceList;
    std::unordered_set<Point> pathing_point_list;

    Point bestPtSoFar;

   public:
    TeleportPathReducer(const std::shared_ptr<TestActMap>& m, int _range = 20)
        : map(m), range(_range * 10), justExpand(false), bestPtSoFar{.x = 0, .y = 0} {
        distanceList.clear();

        const int r = range / 10;
        for (int x = 0 - r; x <= 0 + r; x++) {
            for (int y = 0 - r; y <= 0 + r; y++) {
                if (Euclidean(Point{.x = x, .y = y}, Point{.x = 0, .y = 0}) < range &&
                    Euclidean(Point{.x = x, .y = y}, Point{.x = 0, .y = 0}) > range - 5) {
                    distanceList.push_back({.x = x, .y = y});
                }
            }
        }
    }

    ~TeleportPathReducer() override = default;

    void Reduce(const std::vector<Point>& in, std::vector<Point>& out) override {
        auto it = in.begin(), end = in.end();
        out.push_back(*it);
        while (it != end) {
            Point prev = *(out.end() - 1);
            while (it != end && Euclidean(*it, prev) < range)
                ++it;
            --it;
            Point pt;
            pt.x = it->x;
            pt.y = it->y;

            out.push_back(pt);
            ++it;
        }
    }

    void GetOpenNodes(const Point& center, std::vector<Point>& out, const Point& endpoint) override {
        if (Euclidean(endpoint, center) < range - 20) {
            out.push_back(endpoint);
            return;
        }
        if (bestPtSoFar.x == 0)
            bestPtSoFar = center;

        int val = 1000000;
        Point best{.x = 0, .y = 0};
        if (Euclidean(bestPtSoFar, center) < 500) {
            for (auto& j : distanceList) {
                int x = j.x + center.x;
                int y = j.y + center.y;
                if (!Reject(Point{.x = x, .y = y})) {
                    if (val > Euclidean(Point{.x = x, .y = y}, endpoint)) {
                        val = Euclidean(Point{.x = x, .y = y}, endpoint);
                        best = Point{.x = x, .y = y};
                        out.push_back(best);
                    }
                }
            }
            if (best.x != 0 && !pathing_point_list.contains(best) &&
                Euclidean(best, endpoint) < Euclidean(center, endpoint)) {
                pathing_point_list.insert(best);
                out.push_back(best);
                if (Euclidean(best, endpoint) < Euclidean(bestPtSoFar, endpoint))
                    bestPtSoFar = best;
                return;
            }
        }
        justExpand = true;
        // expand point normally if smart tele isn't found
        for (int i = 1; i >= -1; i--) {
            for (int j = 1; j >= -1; j--) {
                if ((i == 0 && j == 0) || Reject(Point{.x = center.x + i, .y = center.y + j}))
                    continue;
                out.push_back({.x = center.x + i, .y = center.y + j});
                pathing_point_list.insert(Point{.x = center.x + i, .y = center.y + j});
            }
        }
    }

    bool Reject(const Point& pt) override { return checkFlag(map->SpaceGetData(pt)); }

    int GetPenalty(const Point& /*pt*/) override { return 0; }

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
