#pragma once

// The bit operations behind the operators scripts/gen_enum_names.py generates for
// every enumeration marked `/// @flags`. The generated operators live in each
// enumeration's own namespace (argument-dependent lookup finds nothing here) and
// forward to these, so a flag set stays its enumeration type through |, & and ~.

#include <type_traits>
#include <utility>

namespace d2bs::utils {

template <typename E>
    requires std::is_enum_v<E>
[[nodiscard, gnu::always_inline]] constexpr E FlagOr(E a, E b) {
    return static_cast<E>(std::to_underlying(a) | std::to_underlying(b));
}

template <typename E>
    requires std::is_enum_v<E>
[[nodiscard, gnu::always_inline]] constexpr E FlagAnd(E a, E b) {
    return static_cast<E>(std::to_underlying(a) & std::to_underlying(b));
}

template <typename E>
    requires std::is_enum_v<E>
[[nodiscard, gnu::always_inline]] constexpr E FlagXor(E a, E b) {
    return static_cast<E>(std::to_underlying(a) ^ std::to_underlying(b));
}

template <typename E>
    requires std::is_enum_v<E>
[[nodiscard, gnu::always_inline]] constexpr E FlagNot(E a) {
    return static_cast<E>(~std::to_underlying(a));
}

// True when every bit of `flag` is set in `value` (so always true for an empty flag).
// A composite mask that means "any of these" wants FlagHasAny.
template <typename E>
    requires std::is_enum_v<E>
[[nodiscard, gnu::always_inline]] constexpr bool FlagHas(E value, E flag) {
    return (std::to_underlying(value) & std::to_underlying(flag)) == std::to_underlying(flag);
}

// True when any bit of `flag` is set in `value` (so always false for an empty flag).
template <typename E>
    requires std::is_enum_v<E>
[[nodiscard, gnu::always_inline]] constexpr bool FlagHasAny(E value, E flag) {
    return (std::to_underlying(value) & std::to_underlying(flag)) != 0;
}

}  // namespace d2bs::utils
