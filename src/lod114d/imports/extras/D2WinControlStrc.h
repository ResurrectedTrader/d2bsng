#pragma once

#include "game/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>

// D2WinTextBoxLineStrc lives in D2MOO's <D2WinTextBox.h>, but pulling that
// transitively imports Storm.h, which double-defines SMSGHANDLER_PARAMS
// vs <D2Structs.OtherDLLs.h>. We only use it via pointer here, so a forward
// declaration is enough; the consumer that walks pColumns/pNext fields
// (src/lod114d/game/Control.cpp) includes the full header itself.
struct D2WinTextBoxLineStrc;

// D2Win UI struct - D2MOO does not vendor a full D2Win control header (only
// the per-text-line struct), so D2WinControlStrc is authored here from
// reverse-engineered field offsets verified by static_assert.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match binary layout
namespace d2bs::imports::extras {

// D2Win Control - OOG UI widget (size = 0x264).
// NOT packed - uses default alignment.
struct D2WinControlStrc {
    d2bs::game::ControlType dwType;  // 0x00
    uint32_t* _1;                    // 0x04 - ptr, usually points to 6 when dwType is 6
    uint32_t dwDisabled;             // 0x08
    d2bs::game::Rect rect;           // 0x0C - origin.x@+0, origin.y@+4, size.width@+8, size.height@+C
    // Handler table (_2 through _9)
    uint32_t* _2;                         // 0x1C - some sort of function (maybe click?)
    uint32_t _3;                          // 0x20
    uint32_t* _4;                         // 0x24 - some sort of function
    uint32_t* _5;                         // 0x28
    uint32_t _6;                          // 0x2C
    uint32_t* _7;                         // 0x30 - ptr to something
    uint32_t* _8;                         // 0x34 - another random ptr
    uint32_t _9;                          // 0x38
    D2WinControlStrc* pNext;              // 0x3C
    uint32_t _10;                         // 0x40
    uint32_t unkState;                    // 0x44 - 0 when button avail, 1 when greyed
    D2WinTextBoxLineStrc* pFirstText;     // 0x48
    D2WinTextBoxLineStrc* pLastText;      // 0x4C
    D2WinTextBoxLineStrc* pSelectedText;  // 0x50
    uint32_t dwSelectEnd;                 // 0x54
    uint32_t dwSelectStart;               // 0x58
    union {
        struct {                             // Textboxes
            std::array<wchar_t, 256> wText;  // 0x5C (512 bytes)
            uint32_t dwCursorPos;            // 0x25C
            uint32_t dwIsCloaked;            // 0x260
        };
        struct {                              // Buttons
            std::array<uint32_t, 2> _12;      // 0x5C (8 bytes)
            std::array<wchar_t, 256> wText2;  // 0x64 (512 bytes)
        };
    };
};

static_assert(sizeof(D2WinControlStrc) == 0x264, "D2WinControlStrc must be 0x264 bytes");
static_assert(offsetof(D2WinControlStrc, dwType) == 0x00, "D2WinControlStrc::dwType offset drift");
static_assert(offsetof(D2WinControlStrc, dwDisabled) == 0x08, "D2WinControlStrc::dwDisabled offset drift");
static_assert(offsetof(D2WinControlStrc, rect) == 0x0C, "D2WinControlStrc::rect offset drift");
static_assert(offsetof(D2WinControlStrc, pNext) == 0x3C, "D2WinControlStrc::pNext offset drift");
static_assert(offsetof(D2WinControlStrc, unkState) == 0x44, "D2WinControlStrc::unkState offset drift");
static_assert(offsetof(D2WinControlStrc, pFirstText) == 0x48, "D2WinControlStrc::pFirstText offset drift");
static_assert(offsetof(D2WinControlStrc, dwSelectEnd) == 0x54, "D2WinControlStrc::dwSelectEnd offset drift");
static_assert(offsetof(D2WinControlStrc, dwSelectStart) == 0x58, "D2WinControlStrc::dwSelectStart offset drift");
static_assert(offsetof(D2WinControlStrc, wText) == 0x5C, "D2WinControlStrc::wText offset drift");
static_assert(offsetof(D2WinControlStrc, dwCursorPos) == 0x25C, "D2WinControlStrc::dwCursorPos offset drift");
static_assert(offsetof(D2WinControlStrc, dwIsCloaked) == 0x260, "D2WinControlStrc::dwIsCloaked offset drift");
static_assert(offsetof(D2WinControlStrc, wText2) == 0x64, "D2WinControlStrc::wText2 offset drift");

}  // namespace d2bs::imports::extras

// NOLINTEND(readability-identifier-naming)
