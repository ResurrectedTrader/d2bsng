#pragma once

#include <v8.h>

#include <optional>
#include <string>
#include <variant>

#include "api/core/V8Convert.h"
#include "api/globals/TxtLookup.h"
#include "game/GameHelpers.h"

// Shared marshaling for the .txt-table JS surface (the global getBaseStat and
// the TxtTables class bind to these). Resolution is name- or index-based and
// tolerant of bad args: callers map nullopt / empty handles to a JS undefined.

namespace d2bs::api::globals {

// Resolve a table arg to a canonical name: a string name, or a number indexing
// TXT_TABLE_NAMES. nullopt when the arg is the wrong type or the index is out of range.
inline std::optional<std::string> ResolveTableArg(v8::Isolate* isolate, v8::Local<v8::Value> arg) {
    if (arg->IsString()) {
        return v8_convert::ToString(isolate, arg);
    }
    if (arg->IsNumber()) {
        if (auto resolved = ResolveTxtTable(v8_convert::ToUint32(isolate, arg))) {
            return std::string(*resolved);
        }
    }
    return std::nullopt;
}

// Convert one resolved cell to a JS value. Returns an empty handle for an empty
// / unsupported cell (monostate) so callers can map it to undefined or omit it.
inline v8::Local<v8::Value> TxtValueToV8(v8::Isolate* isolate, const game::TxtValue& value) {
    static_assert(std::variant_size_v<game::TxtValue> == 3,
                  "TxtValue alternatives changed - update the conversion below");
    if (const auto* n = std::get_if<int64_t>(&value)) {
        return v8_convert::ToV8(isolate, static_cast<double>(*n));
    }
    if (const auto* s = std::get_if<std::string>(&value)) {
        return v8_convert::ToV8(isolate, *s);
    }
    return {};
}

// Resolve a single cell from an already-resolved table name + row and a column
// arg (string name or numeric index). Undefined for a bad column arg /
// unresolved index / empty cell. Shared by getBaseStat (3-arg) and TxtTables.value.
inline v8::Local<v8::Value> ResolveTxtCell(v8::Isolate* isolate, const std::string& tableName, uint32_t row,
                                           v8::Local<v8::Value> columnArg) {
    std::string columnName;
    if (columnArg->IsString()) {
        columnName = v8_convert::ToString(isolate, columnArg);
    } else if (columnArg->IsNumber()) {
        auto resolved = ResolveTxtColumn(tableName, v8_convert::ToUint32(isolate, columnArg));
        if (!resolved) {
            return v8::Undefined(isolate);
        }
        columnName = std::string(*resolved);
    } else {
        return v8::Undefined(isolate);
    }
    auto cell = TxtValueToV8(isolate, game::GetTxtValue(tableName, row, columnName));
    return cell.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : cell;
}

// Build a {column: value} object for one table row, or undefined if the table
// is unknown or `row` is past the live row count (so an out-of-range id yields
// undefined rather than an all-empty object). Empty / unsupported cells are
// omitted. Shared by getBaseStat's 2-arg form and TxtTables.row.
inline v8::Local<v8::Value> BuildTxtRow(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                        const std::string& tableName, uint32_t row) {
    auto columns = ResolveTxtColumns(tableName);
    if (!columns) {
        return v8::Undefined(isolate);
    }
    auto rowCount = game::GetTxtTableRowCount(tableName);
    if (!rowCount || row >= *rowCount) {
        return v8::Undefined(isolate);
    }
    auto obj = v8::Object::New(isolate);
    for (const auto& column : *columns) {
        auto cell = TxtValueToV8(isolate, game::GetTxtValue(tableName, row, column));
        if (!cell.IsEmpty()) {
            obj->Set(context, v8_convert::ToV8(isolate, column), cell).Check();
        }
    }
    return obj;
}

}  // namespace d2bs::api::globals
