#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "api/globals/TxtTables.h"
#include "utils/utils.h"

namespace d2bs::api::globals {

// Table index -> canonical .txt table name. nullopt if out of range.
constexpr std::optional<std::string_view> ResolveTxtTable(uint32_t tableIdx) {
    if (tableIdx >= d2bs::game::TXT_TABLE_NAMES.size()) {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - runtime table index, bounds checked above
    return d2bs::game::TXT_TABLE_NAMES[tableIdx];
}

// (Table name, column index) -> canonical column name.
// Table name comparison is case-insensitive (matches reference d2bs _strcmpi behavior in
// MPQStats.cpp FillBaseStat). Generated table names in TXT_TABLE_NAMES are already lowercase,
// so only the caller-supplied name needs to be lowered.
// nullopt if the table name is unknown or the column index is out of range for that table.
inline std::optional<std::string_view> ResolveTxtColumn(std::string_view tableName, uint32_t colIdx) {
    std::string lowered = d2bs::utils::ToLower(std::string(tableName));
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - parallel-array lookup by runtime name match
    for (size_t i = 0; i < d2bs::game::TXT_TABLE_NAMES.size(); ++i) {
        if (d2bs::game::TXT_TABLE_NAMES[i] == lowered) {
            const auto& cols = d2bs::game::TXT_COLUMNS_BY_TABLE[i];
            if (colIdx >= cols.size()) {
                return std::nullopt;
            }
            return cols[colIdx];
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return std::nullopt;
}

}  // namespace d2bs::api::globals
