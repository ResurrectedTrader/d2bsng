#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The string helpers, split out from utils.h/.cpp so a consumer can take them
// without taking spdlog. utils.h includes this, so nothing that already
// includes utils.h has to change.
//
// The split is not cosmetic: spdlog reaches these headers through vcpkg's fmt,
// and a frontend built against an engine that vendors a different fmt cannot
// compile a translation unit that sees both. game/Finders.h needs exactly one
// of these functions, and including all of utils.h for it put spdlog into the
// whole game-handle layer.

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
}  // namespace d2bs::utils
