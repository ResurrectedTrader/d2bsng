#pragma once

#include <optional>
#include <string>
#include <variant>

#include "api/core/Convert.h"
#include "api/globals/TxtLookup.h"
#include "game/GameHelpers.h"
#include "unibind/unibind.h"

// Shared marshaling for the .txt-table JS surface (the global getBaseStat and
// the TxtTables class bind to these). Resolution is name- or index-based and
// tolerant of bad args: callers map nullopt / empty handles to a JS undefined.

namespace d2bs::api::globals {

// Resolve a table arg to a canonical name: a string name, or a number indexing
// TXT_TABLE_NAMES. nullopt when the arg is the wrong type or the index is out of range.
inline std::optional<std::string> ResolveTableArg(const ub::Context& context, const ub::Local<ub::Value>& arg) {
    if (arg.IsString()) {
        return convert::ToString(context, arg);
    }
    if (arg.IsNumber()) {
        if (auto resolved = ResolveTxtTable(convert::ToUint32(context, arg))) {
            return std::string(*resolved);
        }
    }
    return std::nullopt;
}

// Convert one resolved cell to a JS value. Returns an empty handle for an empty
// / unsupported cell (monostate) so callers can map it to undefined or omit it.
inline ub::Local<ub::Value> TxtValueToJS(ub::Isolate& isolate, const game::TxtValue& value) {
    static_assert(std::variant_size_v<game::TxtValue> == 3,
                  "TxtValue alternatives changed - update the conversion below");
    if (const auto* n = std::get_if<int64_t>(&value)) {
        return convert::ToJS(isolate, static_cast<double>(*n));
    }
    if (const auto* s = std::get_if<std::string>(&value)) {
        return convert::ToJS(isolate, *s);
    }
    return {};
}

// Resolve a single cell from an already-resolved table name + row and a column
// arg (string name or numeric index). Undefined for a bad column arg /
// unresolved index / empty cell. Shared by getBaseStat (3-arg) and TxtTables.value.
inline ub::Local<ub::Value> ResolveTxtCell(const ub::Context& context, const std::string& tableName, uint32_t row,
                                           const ub::Local<ub::Value>& columnArg) {
    auto& isolate = context.GetIsolate();
    std::string columnName;
    if (columnArg.IsString()) {
        columnName = convert::ToString(context, columnArg);
    } else if (columnArg.IsNumber()) {
        auto resolved = ResolveTxtColumn(tableName, convert::ToUint32(context, columnArg));
        if (!resolved) {
            return ub::Undefined(isolate);
        }
        columnName = std::string(*resolved);
    } else {
        return ub::Undefined(isolate);
    }
    auto cell = TxtValueToJS(isolate, game::GetTxtValue(tableName, row, columnName));
    return cell.IsEmpty() ? ub::Local<ub::Value>(ub::Undefined(isolate)) : cell;
}

// Build a {column: value} object for one table row, or undefined if the table
// is unknown or `row` is past the live row count (so an out-of-range id yields
// undefined rather than an all-empty object). Empty / unsupported cells are
// omitted. Shared by getBaseStat's 2-arg form and TxtTables.row. Empty if the
// object could not be built (an exception may be pending).
inline std::optional<ub::Local<ub::Value>> BuildTxtRow(const ub::Context& context, const std::string& tableName,
                                                       uint32_t row) {
    auto& isolate = context.GetIsolate();
    auto columns = ResolveTxtColumns(tableName);
    if (!columns) {
        return ub::Undefined(isolate);
    }
    auto rowCount = game::GetTxtTableRowCount(tableName);
    if (!rowCount || row >= *rowCount) {
        return ub::Undefined(isolate);
    }
    auto obj = ub::Object::New(context);
    if (!obj) {
        return std::nullopt;
    }
    for (const auto& column : *columns) {
        auto cell = TxtValueToJS(isolate, game::GetTxtValue(tableName, row, column));
        if (!cell.IsEmpty() && !obj->Set(context, column, cell).value_or(false)) {
            return std::nullopt;
        }
    }
    return *obj;
}

}  // namespace d2bs::api::globals
