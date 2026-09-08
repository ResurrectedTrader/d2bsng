#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "utils.h"

namespace d2bs::utils {

// ASCII case-insensitive hashing and equality for string-keyed maps, matching the `_strcmpi`
// semantics the game's own table lookups use.
//
// The fold below must stay byte-identical to EqualsCaseInsensitive's, which is why they live
// together: a hash that disagreed with the equality on any input would send equal keys to
// different buckets and lookups would silently miss.
struct CaseInsensitiveHash {
    size_t operator()(std::string_view text) const noexcept {
        // 32-bit FNV-1a. size_t is 32-bit in this build; the constants are chosen to match.
        size_t hash = 2166136261U;
        for (const char c : text) {
            const char lowered = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            hash ^= static_cast<uint8_t>(lowered);
            hash *= 16777619U;
        }
        return hash;
    }
};

struct CaseInsensitiveEqual {
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
        return EqualsCaseInsensitive(lhs, rhs);
    }
};

// Keyed by string_view, so the keys are borrowed: whatever they point at has to outlive the map.
// Intended for keys with static storage - string literals, or names in constexpr tables. For
// owning keys, declare an unordered_map<std::string, V, CaseInsensitiveHash, CaseInsensitiveEqual>
// instead; the functors work with either.
template <typename Value>
using CaseInsensitiveMap = std::unordered_map<std::string_view, Value, CaseInsensitiveHash, CaseInsensitiveEqual>;

}  // namespace d2bs::utils
