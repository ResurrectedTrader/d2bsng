#pragma once

#include <Windows.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Strings.h"

#ifndef GIT_VERSION
    #define GIT_VERSION "unknown"
#endif
#ifndef GIT_BRANCH
    #define GIT_BRANCH "unknown"
#endif

namespace d2bs::utils {
// The named logger for a component, created on first use. Names are dotted and
// follow the source tree - "hooks.manager", "core.proxy", "script.cache".
//
// Every logger shares one fan-out sink, so where output goes is decided once,
// by AddLogSink, and does not depend on whether a logger was created before or
// after the host installed its sinks.
std::shared_ptr<spdlog::logger> GetLogger(const std::string &name);

// Add a destination every logger writes to, including the ones that already
// exist. The host calls this once it knows where output belongs; anything
// logged before that is dropped rather than misrouted. spdlog's own default
// logger is reached only once the host points it at a GetLogger logger.
void AddLogSink(const spdlog::sink_ptr &sink);

// FILEVERSION of a loaded module's VERSIONINFO resource (14,0,3,0 -> {14, 0, 3, 0}).
struct ModuleVersion {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t build = 0;
    uint16_t revision = 0;

    bool operator==(const ModuleVersion &) const = default;
};

// nullopt when the module has no version resource.
std::optional<ModuleVersion> GetModuleVersion(HMODULE module);

// Whether `address` lies within a loaded module's image.
bool IsInsideModule(HMODULE module, uintptr_t address);

// Typed read of process memory at `address`, without alignment assumptions.
template <typename T>
T ReadValue(uintptr_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void *>(address), sizeof(T));
    return value;
}
}  // namespace d2bs::utils
