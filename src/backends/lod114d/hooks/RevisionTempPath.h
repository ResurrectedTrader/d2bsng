#pragma once
#include <Windows.h>

namespace d2bs::lod114d::hooks {
// GetTempPathA-compatible success convention; every failure returns zero.
DWORD GetRevisionTempPath(DWORD bufferLength, LPSTR buffer);
}  // namespace d2bs::lod114d::hooks
