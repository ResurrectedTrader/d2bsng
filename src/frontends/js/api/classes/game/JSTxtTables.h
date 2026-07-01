#pragma once

#include <v8.h>

#include <string_view>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/globals/TxtTableAccess.h"
#include "game/GameHelpers.h"

namespace d2bs::api::classes {

// Native payload for the TxtTables class. The class is a pure static namespace
// (never instantiated), so this carries no state - it exists only to satisfy
// the V8ClassBase NativeType parameter.
struct TxtTablesData {};

// `TxtTables`: a non-constructable namespace class exposing the Diablo II data
// (.txt) tables through static methods - the same machinery as the global
// getBaseStat, with method names that don't stutter. Used from scripts as
// `TxtTables.names()`, `TxtTables.row("runes", 42)`, etc.
class JSTxtTables : public V8ClassBase<JSTxtTables, TxtTablesData> {
   public:
    static constexpr std::string_view ClassName = "TxtTables";
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        using namespace d2bs::api::globals;  // ResolveTableArg / ResolveTxtColumns / ResolveTxtCell / BuildTxtRow

        /// @description List every known data (.txt) table name.
        /// @signature TxtTables.names()
        /// @returns {Array<string>} - the table names, usable as the `table` argument to the other methods
        StaticMethod(
            isolate, tpl, "names", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto arr = v8::Array::New(isolate, game::TXT_TABLE_NAMES.size());
                uint32_t i = 0;
                for (const auto& name : game::TXT_TABLE_NAMES) {
                    arr->Set(context, i++, v8_convert::ToV8(isolate, name)).Check();
                }
                args.GetReturnValue().Set(arr);
            });

        /// @description Number of rows in a table.
        /// @signature TxtTables.size(table)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @returns {number|undefined} - the row count, or undefined if the table is unknown or its data is not loaded
        StaticMethod(
            isolate, tpl, "size", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (args.Length() < 1) {
                    return;
                }
                auto table = ResolveTableArg(isolate, args[0]);
                if (!table) {
                    return;
                }
                if (auto count = game::GetTxtTableRowCount(*table)) {
                    args.GetReturnValue().Set(v8_convert::ToV8(isolate, *count));
                }
            });

        /// @description List a table's column names, in column order.
        /// @signature TxtTables.columns(table)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @returns {Array<string>|undefined} - the column names, or undefined if the table is unknown
        StaticMethod(
            isolate, tpl, "columns", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                if (args.Length() < 1) {
                    return;
                }
                auto table = ResolveTableArg(isolate, args[0]);
                if (!table) {
                    return;
                }
                auto columns = ResolveTxtColumns(*table);
                if (!columns) {
                    return;
                }
                auto arr = v8::Array::New(isolate, static_cast<int32_t>(columns->size()));
                uint32_t i = 0;
                for (const auto& column : *columns) {
                    arr->Set(context, i++, v8_convert::ToV8(isolate, column)).Check();
                }
                args.GetReturnValue().Set(arr);
            });

        /// @description Read a whole row as an object mapping each column name to its value.
        /// @signature TxtTables.row(table, row)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @param row {number} - row index
        /// @returns {object|undefined} - a {column: value} object, or undefined if the table is unknown or the row is
        /// out of range
        StaticMethod(
            isolate, tpl, "row", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (args.Length() < 2) {
                    return;
                }
                auto table = ResolveTableArg(isolate, args[0]);
                if (!table) {
                    return;
                }
                uint32_t row = v8_convert::ToUint32(isolate, args[1]);
                args.GetReturnValue().Set(BuildTxtRow(isolate, isolate->GetCurrentContext(), *table, row));
            });

        /// @description Read a single cell (the same lookup as getBaseStat with a column).
        /// @signature TxtTables.value(table, row, column)
        /// @param table {string|number} - table name, or index into TxtTables.names()
        /// @param row {number} - row index
        /// @param column {string|number} - column name, or index into the table's columns
        /// @returns {number|string|undefined} - the cell value, or undefined if unresolved or the cell is empty
        StaticMethod(
            isolate, tpl, "value", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (args.Length() < 3) {
                    return;
                }
                auto table = ResolveTableArg(isolate, args[0]);
                if (!table) {
                    return;
                }
                uint32_t row = v8_convert::ToUint32(isolate, args[1]);
                args.GetReturnValue().Set(ResolveTxtCell(isolate, *table, row, args[2]));
            });
    }
};

}  // namespace d2bs::api::classes
