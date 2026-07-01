#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include <Windows.h>  // HWND

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2gfx {

// ---- Functions -------------------------------------------------------------
inline StdcallFunc<void(int32_t /*nX1*/, int32_t /*nY1*/, int32_t /*nX2*/, int32_t /*nY2*/, uint32_t /*dwColor*/,
                        uint32_t /*dwTrans*/)>
    D2GFX_DrawRectangle{0xF6300};
// MOO: D2GFX_DrawLine(int32_t nXStart, int32_t nYStart, int32_t nXEnd, int32_t nYEnd, uint8_t nColor, uint8_t nAlpha).
inline StdcallFunc<void(int32_t /*nX1*/, int32_t /*nY1*/, int32_t /*nX2*/, int32_t /*nY2*/, uint32_t /*dwColor*/,
                        uint32_t /*nAlpha*/)>
    D2GFX_DrawLine{0xF6380};
inline StdcallFunc<void(void* /*pContext*/, uint32_t /*nX*/, uint32_t /*nY*/, uint32_t /*nBright2*/,
                        uint32_t /*nBright*/, uint8_t* /*pColorTable*/)>
    D2GFX_DrawAutomapCell{0xF6480};
inline StdcallFunc<HWND()> WINDOW_GetWindow{0xF59A0};
inline StdcallFunc<uint32_t()> D2GFX_GetResolutionMode{0xF5160};

}  // namespace d2bs::imports::d2gfx
// NOLINTEND(readability-identifier-naming)
