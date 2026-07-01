#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include "D2MOOConfig.h"
#include "ImportTypes.h"

#include "extras/D2WinControlStrc.h"

#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions
// sourced from reference/d2bs/D2Ptrs.h.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2win {

// ---- Functions -------------------------------------------------------------
inline FastcallFunc<void*(extras::D2WinControlStrc* /*pControl*/, const wchar_t* /*wText*/)> CONTROL_SetText{0xFF5A0};
inline FastcallFunc<void()> D2WIN_DrawSprites{0xF9870};
inline FastcallFunc<void*(const char* /*szFile*/, int32_t /*nType*/)> ARCHIVE_LoadCellFile{0xFA9B0};
inline FastcallFunc<void()> D2WIN_TakeScreenshot{0xFA7A0};
// MOO: D2Win_10117_DrawText(const Unicode* wszText, int nX, int nY, int nColor, BOOL bCentered).
inline FastcallFunc<void(const wchar_t* /*wStr*/, int32_t /*nX*/, int32_t /*nY*/, uint32_t /*dwColor*/,
                         uint32_t /*bCentered*/)>
    D2WIN_DrawText{0x102320};
inline FastcallFunc<uint32_t(const wchar_t* /*wStr*/, uint32_t* /*pWidth*/, uint32_t* /*pFileNo*/)> D2WIN_GetTextSize{
    0x102520};
inline FastcallFunc<uint32_t(uint32_t /*dwSize*/)> D2WIN_SetTextSize{0x102EF0};
// MOO: ARCHIVE_LoadMPQFile(szModuleName, szFileName, szLabel, a4, HANDLE hFile,
// ARCHIVE_ShowMessageFunctionPtr pfShowMessage, int nPriority). 1.14d's
// monolithic Game.exe drops the szModuleName/szLabel/a4 args (no DLL-based
// loading); positions 5/6/7 of MOO line up with args 2/3/4 here. The current
// `const char*` type on arg 2 is reference d2bs's mistake (every caller passes
// 0); per MOO it is `HANDLE hFile`. Bit pattern matches at the ABI boundary.
inline FastcallFunc<uint32_t(const char* /*szMpqFile*/, const char* /*szMpqName*/, int32_t /*pfShowMessage*/,
                             int32_t /*nPriority*/)>
    ARCHIVE_InitMPQ{0x117332};

// ---- Variables -------------------------------------------------------------
inline GameVar<extras::D2WinControlStrc*> gpFirstControl{0x3D55BC};

}  // namespace d2bs::imports::d2win
// NOLINTEND(readability-identifier-naming)
