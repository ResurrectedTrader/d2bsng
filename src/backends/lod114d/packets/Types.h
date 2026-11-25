#pragma once

// The coordinate pairs the 1.14d wire protocol carries. The width is part of the layout, so each width is its own type.

#include <cstdint>

namespace d2bs::lod114d::packets {

struct Point16 {
    uint16_t x = 0;
    uint16_t y = 0;
};

static_assert(sizeof(Point16) == 4);

struct Point32 {
    uint32_t x = 0;
    uint32_t y = 0;
};

static_assert(sizeof(Point32) == 8);

}  // namespace d2bs::lod114d::packets
