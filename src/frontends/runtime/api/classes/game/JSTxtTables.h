#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/globals/TxtTableAccess.h"
#include "game/GameHelpers.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Native payload for the TxtTables class. The class is a pure static namespace
// (never instantiated), so this carries no state - it exists only to satisfy
// the ClassBase NativeType parameter.
struct TxtTablesData {};

// `TxtTables`: a non-constructable namespace class exposing the Diablo II data
// (.txt) tables through static methods - the same machinery as the global
// getBaseStat, with method names that don't stutter. Used from scripts as
// `TxtTables.names()`, `TxtTables.row("runes", 42)`, etc.
class JSTxtTables : public ClassBase<JSTxtTables, TxtTablesData> {
   public:
    static constexpr std::string_view ClassName = "TxtTables";

    static void Configure(const ub::Class<Native>& cls) {
        using namespace d2bs::api::globals;  // ResolveTableArg / ResolveTxtColumns / ResolveTxtCell / BuildTxtRow

        /// @description List every known data (.txt) table name.
        /// @signature TxtTables.names()
        /// @returns {Array<string>} - the table names, usable as the `table` argument to the other methods
        StaticMethod(
            cls, "names", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                auto arr = ub::Array::New(context, static_cast<uint32_t>(game::TXT_TABLE_NAMES.size()));
                if (!arr) {
                    return;
                }
                uint32_t i = 0;
                for (const auto& name : game::TXT_TABLE_NAMES) {
                    if (!arr->Set(context, i++, convert::ToJS(isolate, name)).value_or(false)) {
                        return;
                    }
                }
                args.GetReturnValue().Set(*arr);
            });

        /// @description Number of rows in a table.
        /// @signature TxtTables.size(table)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @returns {number|undefined} - the row count, or undefined if the table is unknown or its data is not loaded
        StaticMethod(
            cls, "size", +[](const ub::CallbackInfo& args) {
                if (args.Length() < 1) {
                    return;
                }
                auto table = ResolveTableArg(args.GetContext(), args[0]);
                if (!table) {
                    return;
                }
                if (auto count = game::GetTxtTableRowCount(*table)) {
                    args.GetReturnValue().Set(*count);
                }
            });

        /// @description List a table's column names, in column order.
        /// @signature TxtTables.columns(table)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @returns {Array<string>|undefined} - the column names, or undefined if the table is unknown
        StaticMethod(
            cls, "columns", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                if (args.Length() < 1) {
                    return;
                }
                auto table = ResolveTableArg(context, args[0]);
                if (!table) {
                    return;
                }
                auto columns = ResolveTxtColumns(*table);
                if (!columns) {
                    return;
                }
                auto arr = ub::Array::New(context, static_cast<uint32_t>(columns->size()));
                if (!arr) {
                    return;
                }
                uint32_t i = 0;
                for (const auto& column : *columns) {
                    if (!arr->Set(context, i++, convert::ToJS(isolate, column)).value_or(false)) {
                        return;
                    }
                }
                args.GetReturnValue().Set(*arr);
            });

        /// @description Read a whole row as an object mapping each column name to its value.
        /// @signature TxtTables.row(table, row)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @param row {number} - row index
        /// @returns {object|undefined} - a {column: value} object, or undefined if the table is unknown or the row is
        /// out of range
        StaticMethod(
            cls, "row", +[](const ub::CallbackInfo& args) {
                const auto& context = args.GetContext();
                if (args.Length() < 2) {
                    return;
                }
                auto table = ResolveTableArg(context, args[0]);
                if (!table) {
                    return;
                }
                uint32_t row = convert::ToUint32(context, args[1]);
                Return(args, BuildTxtRow(context, *table, row));
            });

        /// @description Read a single cell (the same lookup as getBaseStat with a column).
        /// @signature TxtTables.value(table, row, column)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @param row {number} - row index
        /// @param column {string|number} - column name, or index into the table's columns
        /// @returns {number|string|undefined} - the cell value, or undefined if unresolved or the cell is empty
        StaticMethod(
            cls, "value", +[](const ub::CallbackInfo& args) {
                const auto& context = args.GetContext();
                if (args.Length() < 3) {
                    return;
                }
                auto table = ResolveTableArg(context, args[0]);
                if (!table) {
                    return;
                }
                uint32_t row = convert::ToUint32(context, args[1]);
                Return(args, ResolveTxtCell(context, *table, row, args[2]));
            });
    }

   private:
    // A missing value (an empty handle, or nothing at all) leaves the result undefined.
    static void Return(const ub::CallbackInfo& args, const ub::Local<ub::Value>& value) {
        if (!value.IsEmpty()) {
            args.GetReturnValue().Set(value);
        }
    }

    static void Return(const ub::CallbackInfo& args, const std::optional<ub::Local<ub::Value>>& value) {
        if (value) {
            Return(args, *value);
        }
    }
};

}  // namespace d2bs::api::classes
