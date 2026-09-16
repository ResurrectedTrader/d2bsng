#include "utils.h"

#include <Psapi.h>

#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <mutex>
#include <ranges>
#include <vector>

#pragma comment(lib, "version.lib")

namespace d2bs::utils {
std::string ToStr(const std::wstring &str, uint32_t codePage) {
    if (str.empty())
        return {};
    auto inputLen = static_cast<int>(str.size());
    auto len = WideCharToMultiByte(codePage, 0, str.c_str(), inputLen, nullptr, 0, codePage ? nullptr : "?", nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(codePage, 0, str.c_str(), inputLen, result.data(), len, codePage ? nullptr : "?", nullptr);
    return result;
}

std::wstring ToWStr(const std::string &str, uint32_t codePage) {
    if (str.empty())
        return {};
    auto inputLen = static_cast<int>(str.size());
    auto len = MultiByteToWideChar(codePage, 0, str.c_str(), inputLen, nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(codePage, 0, str.c_str(), inputLen, result.data(), len);
    return result;
}

std::string ToLower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return s;
}

std::wstring ToLower(std::wstring s) {
    std::ranges::transform(s, s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    auto fold = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (fold(haystack[i + j]) != fold(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool EqualsCaseInsensitive(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    auto fold = [](char c) -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (fold(lhs[i]) != fold(rhs[i])) {
            return false;
        }
    }
    return true;
}

bool EqualsCaseInsensitive(std::wstring_view lhs, std::wstring_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::towlower(lhs[i]) != std::towlower(rhs[i])) {
            return false;
        }
    }
    return true;
}

std::string_view TrimLeft(std::string_view s, std::string_view chars) {
    auto pos = s.find_first_not_of(chars);
    return pos == std::string_view::npos ? std::string_view{} : s.substr(pos);
}

std::string_view TrimRight(std::string_view s, std::string_view chars) {
    auto pos = s.find_last_not_of(chars);
    return pos == std::string_view::npos ? std::string_view{} : s.substr(0, pos + 1);
}

std::string_view Trim(std::string_view s, std::string_view chars) {
    return TrimRight(TrimLeft(s, chars), chars);
}

std::vector<std::string> Split(std::string_view s, std::string_view separators, size_t maxTokens) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        auto tokenStart = s.find_first_not_of(separators, pos);
        if (tokenStart == std::string_view::npos) {
            break;
        }

        // Cap reached - the final token is the untouched remainder.
        if (maxTokens > 0 && out.size() + 1 == maxTokens) {
            out.emplace_back(s.substr(tokenStart));
            return out;
        }

        auto tokenEnd = s.find_first_of(separators, tokenStart);
        if (tokenEnd == std::string_view::npos) {
            out.emplace_back(s.substr(tokenStart));
            return out;
        }
        out.emplace_back(s.substr(tokenStart, tokenEnd - tokenStart));
        pos = tokenEnd;
    }
    return out;
}

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
