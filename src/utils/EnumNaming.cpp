#include "EnumNaming.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d2bs::utils {

std::string LookupEnumName(std::span<const EnumEntry> entries, uint64_t bits) {
    // The exact enumerator wins over the decomposition: for a plain enum the
    // decomposition happily spells a value out of unrelated enumerators whose bits
    // happen to add up (a UnitType of 3 would read as "Monster|Object").
    const auto exact = std::ranges::find(entries, bits, &EnumEntry::bits);
    if (exact != entries.end()) {
        return std::string(exact->name);
    }

    // The single-bit enumerators set in `bits`, lowest bit first; a name only when
    // together they cover `bits` exactly. Masks and other multi-bit enumerators
    // never take part.
    std::vector<EnumEntry> parts;
    uint64_t covered = 0;
    for (const EnumEntry& entry : entries) {
        if (!std::has_single_bit(entry.bits) || (entry.bits & bits) == 0 || (entry.bits & covered) != 0) {
            continue;
        }
        parts.push_back(entry);
        covered |= entry.bits;
    }
    if (parts.empty() || covered != bits) {
        return {};
    }
    std::ranges::stable_sort(parts, {}, &EnumEntry::bits);

    std::string name;
    for (const EnumEntry& part : parts) {
        if (!name.empty()) {
            name += '|';
        }
        name += part.name;
    }
    return name;
}

}  // namespace d2bs::utils
