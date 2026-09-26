#pragma once

// The shared half of enumeration naming. scripts/gen_enum_names.py generates, per
// project, a <Project>EnumNames.h / .cpp pair that gives every namespace-scope
// enumeration an EnumName(value) overload and a format_as (fmt's hook), both in the
// enumeration's own namespace; each header that defines enumerations includes its
// project's pair. This header supplies the name-table lookup those use and the
// std::formatter, so an enumeration formats by name through std::format and
// fmt / spdlog alike, and EnumName(value) returns the same string.

#include <concepts>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace d2bs::utils {

struct EnumEntry {
    uint64_t bits = 0;
    std::string_view name;
};

// An enum value as the name tables store it: the underlying value widened to 64
// bits (signed values sign-extend, so bitwise tests agree with the underlying type).
template <typename E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr uint64_t EnumBits(E value) {
    return static_cast<uint64_t>(std::to_underlying(value));
}

// The enumerator named by `bits` (the first declared one for aliases), else the
// single-bit enumerators that together make up exactly `bits` joined as "A|B",
// else empty.
[[nodiscard]] std::string LookupEnumName(std::span<const EnumEntry> entries, uint64_t bits);

// LookupEnumName, falling back to "TypeName(value)".
template <std::integral T>
[[nodiscard]] std::string NameEnumValue(std::string_view typeName, std::span<const EnumEntry> entries, T value) {
    if (auto name = LookupEnumName(entries, static_cast<uint64_t>(value)); !name.empty()) {
        return name;
    }
    return std::format("{}({})", typeName, value);
}

// An enumeration with a generated EnumName, found by argument-dependent lookup.
template <typename E>
concept NamedEnum = std::is_enum_v<E> && requires(E value) {
    { EnumName(value) } -> std::same_as<std::string>;
};

}  // namespace d2bs::utils

// Formats a named enumeration as its EnumName; width / alignment specs apply as
// they do to a string.
template <d2bs::utils::NamedEnum E>
// NOLINTNEXTLINE(cert-dcl58-cpp) - specialising std::formatter for program-defined types is allowed
struct std::formatter<E, char> : std::formatter<std::string_view, char> {
    // NOLINTNEXTLINE(readability-identifier-naming) - std::formatter's required member name
    auto format(E value, std::format_context& ctx) const {
        return std::formatter<std::string_view, char>::format(EnumName(value), ctx);
    }
};
