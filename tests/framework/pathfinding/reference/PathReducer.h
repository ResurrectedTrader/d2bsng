#pragma once

// Adapted from a reference A* pathfinding implementation.

#include "Pather.h"

namespace Mapping::Pathing::Reducing {

class PathReducer {
   public:
    virtual ~PathReducer() = default;
    virtual void Reduce(const std::vector<Point>& in, std::vector<Point>& out) = 0;
    virtual void GetOpenNodes(const Point& center, std::vector<Point>& out, const Point& endpoint) = 0;
    virtual bool Reject(const Point& pt) = 0;
    virtual int GetPenalty(const Point& pt) = 0;
    virtual void MutatePoint(Point& pt) = 0;
};

}  // namespace Mapping::Pathing::Reducing
