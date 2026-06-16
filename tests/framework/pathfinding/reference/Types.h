#pragma once

// Adapted from a reference A* pathfinding implementation.
// Provides the Point type and collision constants used by
// the reference A* implementation, with NO game dependencies.

#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "components/pathfinding/Pathfinder.h"

using d2bs::pathfinding::Point;
