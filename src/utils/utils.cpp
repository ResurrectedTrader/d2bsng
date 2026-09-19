#include "utils.h"

#include <Psapi.h>

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <array>
#include <mutex>
#include <vector>

#pragma comment(lib, "version.lib")

namespace d2bs::utils {

namespace {

// One fan-out sink shared by every logger. Sinks are added to it as the host
// works out where output belongs, so a logger created at DLL attach and one
// created after the file is open write to the same places. Without this, a
// logger would capture whatever the default logger's sinks happened to be at
// the moment it was created.
std::shared_ptr<spdlog::sinks::dist_sink_mt> &SharedSink() {
    static auto sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    return sink;
}

std::mutex &LoggerMutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

std::shared_ptr<spdlog::logger> GetLogger(const std::string &name) {
    std::scoped_lock lock(LoggerMutex());

    if (auto instance = spdlog::get(name); instance != nullptr) {
        return instance;
    }
    auto logger = std::make_shared<spdlog::logger>(name, SharedSink());
    logger->enable_backtrace(100);
    // Flushing per record would put an fflush on every log line, inside the
    // shared sink's lock - a script logging in a loop would stall whatever
    // else is logging, including the game thread. The periodic flush the host
    // installs bounds how much can be lost instead; this only forces the
    // entries worth having on disk even if the next second never arrives.
    logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(logger);
    return logger;
}

void AddLogSink(const spdlog::sink_ptr &sink) {
    std::scoped_lock lock(LoggerMutex());
    SharedSink()->add_sink(sink);
}

std::optional<ModuleVersion> GetModuleVersion(HMODULE module) {
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), path.size());
    if (length == 0 || length >= path.size()) {
        return std::nullopt;
    }
    const DWORD size = GetFileVersionInfoSizeW(path.data(), nullptr);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> buffer(size);
    if (GetFileVersionInfoW(path.data(), 0, size, buffer.data()) == 0) {
        return std::nullopt;
    }
    VS_FIXEDFILEINFO *info = nullptr;
    UINT infoLength = 0;
    if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void **>(&info), &infoLength) == 0 || info == nullptr ||
        infoLength < sizeof(VS_FIXEDFILEINFO)) {
        return std::nullopt;
    }
    return ModuleVersion{.major = HIWORD(info->dwFileVersionMS),
                         .minor = LOWORD(info->dwFileVersionMS),
                         .build = HIWORD(info->dwFileVersionLS),
                         .revision = LOWORD(info->dwFileVersionLS)};
}

bool IsInsideModule(HMODULE module, uintptr_t address) {
    MODULEINFO info{};
    if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) == 0) {
        return false;
    }
    const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
    return address >= base && address < base + info.SizeOfImage;
}
}  // namespace d2bs::utils
