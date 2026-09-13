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

#ifndef GIT_VERSION
    #define GIT_VERSION "unknown"
#endif
#ifndef GIT_BRANCH
    #define GIT_BRANCH "unknown"
#endif

namespace d2bs::utils {
std::string ToStr(const std::wstring &str, uint32_t codePage = CP_UTF8);

std::wstring ToWStr(const std::string &str, uint32_t codePage = CP_UTF8);

std::string ToLower(std::string s);
std::wstring ToLower(std::wstring s);

// Return true if `needle` occurs anywhere in `haystack`, ignoring ASCII case.
// Empty `needle` is treated as a match (mirrors std::string::find).
[[nodiscard]] bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle);

// Return true if `lhs` and `rhs` are equal, ignoring ASCII case. Equivalent to
// the Win32 `_strcmpi(a, b) == 0` idiom but operates on string_views without
// allocating temporaries (unlike `ToLower(a) == ToLower(b)`).
[[nodiscard]] bool EqualsCaseInsensitive(std::string_view lhs, std::string_view rhs);
[[nodiscard]] bool EqualsCaseInsensitive(std::wstring_view lhs, std::wstring_view rhs);

// Drop leading characters in `chars` from `s`. Default set is ASCII whitespace.
std::string_view TrimLeft(std::string_view s, std::string_view chars = " \t");

// Drop trailing characters in `chars` from `s`. Default set is ASCII whitespace.
std::string_view TrimRight(std::string_view s, std::string_view chars = " \t");

// Drop leading and trailing characters in `chars` from `s`.
std::string_view Trim(std::string_view s, std::string_view chars = " \t");

// Split `s` on any character in `separators`. Empty tokens (from leading
// or consecutive separators) are dropped.
//
// If `maxTokens > 0`, at most that many tokens are produced; the final
// token is the remainder of `s` starting at the first non-separator after
// the last full token and extending to end-of-string (including any
// embedded separators - they are NOT further split).
//
// If `maxTokens == 0`, no cap.
std::vector<std::string> Split(std::string_view s, std::string_view separators, size_t maxTokens = 0);

std::shared_ptr<spdlog::logger> GetLogger(const std::string &name);

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
