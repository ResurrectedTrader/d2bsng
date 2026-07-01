#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// Schema-driven dispatch for D2 .txt table cell lookups (items / monstats /
// skills / levels / etc.). Table descriptors and field schemas are 1.14d
// facts (column offsets within the typed record); the dispatch is a fresh
// rewrite that delegates field-width handling to a small set of FieldKind
// readers.
//
// Lookup contract: given (tableName, recordId, columnName) returns one of
//   * monostate - unknown table / column / out-of-range record / unsupported
//                 column type (FIELDTYPE_MONSTER_COMPS, etc.)
//   * int64_t   - numeric column (BYTE / WORD / DWORD / single-bit)
//   * string    - ASCII column (FIELDTYPE_DATA_ASCII) or 4-byte item code
//                 column (FIELDTYPE_DATA_RAW / FIELDTYPE_ASCII_TO_CODE)

namespace d2bs::imports::extras {

using TxtValue = std::variant<std::monostate, int64_t, std::string>;

[[nodiscard]] TxtValue GetTxtValue(std::string_view tableName, uint32_t recordId, std::string_view columnName);

// Number of rows in the named table. nullopt when the table name is unknown or
// the underlying game data table is not yet loaded.
[[nodiscard]] std::optional<uint32_t> GetTxtTableRowCount(std::string_view tableName);

}  // namespace d2bs::imports::extras
