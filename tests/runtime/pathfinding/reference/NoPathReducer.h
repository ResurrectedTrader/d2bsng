#pragma once

// Adapted from a reference A* pathfinding implementation.
// Changes: replaced ActMap with TestActMap, removed game includes.
// All algorithm logic kept identical.

#include "AStarPath.h"
#include "PathReducer.h"

namespace Mapping::Pathing::Reducing {

class NoPathReducer : public PathReducer {
    std::shared_ptr<TestActMap> map;

   public:
    explicit NoPathReducer(const std::shared_ptr<TestActMap>& map_) : map(map_) {}

    ~NoPathReducer() override = default;

    // the path is not reduced at all
    void Reduce(const std::vector<Point>& in, std::vector<Point>& out) override { out = in; }

    // accept only walkable nodes
    bool Reject(const Point& pt) override { return checkFlag(map->SpaceGetData(pt)); }

    void GetOpenNodes(const Point& center, std::vector<Point>& out, const Point& /*endpoint*/) override {
        for (int i = 1; i >= -1; i--) {
            for (int j = 1; j >= -1; j--) {
                if (i == 0 && j == 0)
                    continue;
                out.push_back({.x = center.x + i, .y = center.y + j});
            }
        }
    }

    int GetPenalty(const Point& /*pt*/) override { return 0; }

    void MutatePoint(Point& pt) override {
        // find the nearest walkable space
        if (Reject(pt)) {
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    if (i == 0 && j == 0)
                        continue;
                    Point ptN{.x = pt.x + i, .y = pt.y + j};
                    if (!Reject(ptN)) {
                        pt.x = ptN.x;
                        pt.y = ptN.y;
                        return;
                    }
                }
            }
        }
    }
};

}  // namespace Mapping::Pathing::Reducing
