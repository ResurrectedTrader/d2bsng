#pragma once

// Adapted from a reference A* pathfinding implementation.
// Stripped of game dependencies (D2Structs.h, D2Helpers.h, ScopedRoomLoader).

#include "Types.h"

namespace Mapping::Pathing {

using Estimator = int(__fastcall*)(const Point&, const Point&);
using Distance = int(__fastcall*)(const Point&, const Point&);

class Pather {
   public:
    virtual ~Pather() = default;
    virtual bool GetPath(const Point& start, const Point& end, std::vector<Point>& list) = 0;
};

}  // namespace Mapping::Pathing
