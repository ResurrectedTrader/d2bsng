#include "RevisionTempPath.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace d2bs::lod114d::hooks {
DWORD GetRevisionTempPath(DWORD bufferLength, LPSTR buffer) {
    if (buffer == nullptr || bufferLength == 0) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    buffer[0] = '\0';
    std::array<char, MAX_PATH> base{};
    const DWORD baseLength = GetTempPathA(static_cast<DWORD>(base.size()), base.data());
    if (baseLength == 0) {
        return 0;
    }
    if (baseLength >= base.size()) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    std::array<char, MAX_PATH> path{};
    // PIDs distinguish simultaneous instances, even duplicate profiles and titles.
    const int length =
        std::snprintf(path.data(), path.size(), "%sd2bs-revision-%lu\\", base.data(), GetCurrentProcessId());
    if (length < 0 || static_cast<size_t>(length) >= path.size() || static_cast<DWORD>(length) >= bufferLength) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if (!CreateDirectoryA(path.data(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return 0;
        }
        const DWORD attrs = GetFileAttributesA(path.data());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            return 0;
        }
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            SetLastError(ERROR_DIRECTORY);
            return 0;
        }
    }
    std::memcpy(buffer, path.data(), static_cast<size_t>(length) + 1);
    return static_cast<DWORD>(length);
}
}  // namespace d2bs::lod114d::hooks
