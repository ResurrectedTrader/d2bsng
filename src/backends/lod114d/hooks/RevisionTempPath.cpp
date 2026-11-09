#include "RevisionTempPath.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>

namespace d2bs::lod114d::hooks {
DWORD GetRevisionTempPath(DWORD bufferLength, LPSTR buffer) {
    if (buffer == nullptr || bufferLength == 0) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    buffer[0] = '\0';
    try {
        std::error_code error;
        const auto base = std::filesystem::temp_directory_path(error);
        if (error) {
            return 0;
        }
        // Process IDs distinguish simultaneous instances even when profiles/titles repeat.
        const auto path = base / std::format("d2bs-revision-{}", GetCurrentProcessId());
        const std::string directory = path.string() + "\\";
        if (directory.size() >= bufferLength || directory.size() >= MAX_PATH) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        std::filesystem::create_directory(path, error);
        if (error) {
            return 0;
        }
        // symlink_status examines the directory entry itself, rather than following a link.
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
            return 0;
        }
        *std::ranges::copy(directory, buffer).out = '\0';
        return static_cast<DWORD>(directory.size());
    } catch (const std::exception&) {
        // Nothing crosses the WINAPI hook boundary, and failure never selects the shared temp path.
        return 0;
    }
}
}  // namespace d2bs::lod114d::hooks
