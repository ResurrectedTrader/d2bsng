#include "utils.h"

#include <Psapi.h>

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <array>
#include <format>
#include <mutex>
#include <string_view>
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

// The first language's FileVersion string ("1.14.3.71") as up to
// four numbers separated by dots or commas; anything after the last number
// (" (79b7ae8)") is ignored. nullopt when there is no such string or it holds
// fewer than two numbers.
std::optional<ModuleVersion> ParseFileVersionString(std::vector<uint8_t> &versionInfo) {
    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation *translation = nullptr;
    UINT translationLength = 0;
    if (VerQueryValueW(versionInfo.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translation),
                       &translationLength) == 0 ||
        translation == nullptr || translationLength < sizeof(Translation)) {
        return std::nullopt;
    }
    const auto key =
        std::format(L"\\StringFileInfo\\{:04x}{:04x}\\FileVersion", translation->language, translation->codePage);
    wchar_t *text = nullptr;
    UINT textLength = 0;
    if (VerQueryValueW(versionInfo.data(), key.c_str(), reinterpret_cast<void **>(&text), &textLength) == 0 ||
        text == nullptr) {
        return std::nullopt;
    }

    std::array<uint32_t, 4> parts{};
    size_t count = 0;
    std::wstring_view rest{text};
    while (count < parts.size()) {
        rest.remove_prefix(std::min(rest.find_first_not_of(L' '), rest.size()));
        const auto digits = std::min(rest.find_first_not_of(L"0123456789"), rest.size());
        if (digits == 0) {
            break;
        }
        uint32_t value = 0;
        for (const wchar_t digit : rest.substr(0, digits)) {
            value = (value * 10) + static_cast<uint32_t>(digit - L'0');
        }
        parts.at(count++) = value;
        rest.remove_prefix(digits);
        rest.remove_prefix(std::min(rest.find_first_not_of(L' '), rest.size()));
        if (rest.empty() || (rest.front() != L'.' && rest.front() != L',')) {
            break;
        }
        rest.remove_prefix(1);
    }
    if (count < 2) {
        return std::nullopt;
    }
    return ModuleVersion{.major = parts[0], .minor = parts[1], .build = parts[2], .revision = parts[3]};
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
    if (const auto parsed = ParseFileVersionString(buffer)) {
        return parsed;
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
