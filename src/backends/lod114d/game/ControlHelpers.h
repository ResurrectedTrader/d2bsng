#pragma once

#include <cstdint>

#include "game/Types.h"
// ReSharper disable once CppUnusedIncludeDirective
#include "imports/D2MOOConfig.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2WinControlHeader.h>  // D2WinControlStrc
#pragma clang diagnostic pop

namespace d2bs::game {

inline ControlType TypeOf(const D2WinControlStrc* ctrl) noexcept {
    return static_cast<ControlType>(ctrl->nType);
}

// The game stores a control's left edge and bottom edge in nImageX / nImageY.
inline Rect BoundsOf(const D2WinControlStrc* ctrl) noexcept {
    return {
        .origin = {.x = static_cast<uint32_t>(ctrl->nImageX), .y = static_cast<uint32_t>(ctrl->nImageY)},
        .size = {.width = static_cast<uint32_t>(ctrl->nWidth), .height = static_cast<uint32_t>(ctrl->nHeight)},
    };
}

}  // namespace d2bs::game
