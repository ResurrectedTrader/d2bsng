#include "JSDBStatement.h"
#include "JSSQLite.h"
#include "SQLiteBind.h"

#include <cstdint>

namespace d2bs::api::classes {

void DBStatementData::Finalize() {
    if (handle && isOpen) {
        sqlite3_finalize(handle);
        handle = nullptr;
        isOpen = false;
    }

    // Remove from parent's statement list if parent still exists
    // This prevents use-after-free when DB iterates over dead statement pointers
    if (parent) {
        parent->statements.erase(this);
        parent = nullptr;
    }

    cachedRow.Reset();
    hasRow = false;
}

void JSDBStatement::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    // Properties
    /// @description The SQL text of the prepared statement.
    /// @type {string}
    Property(
        isolate, inst, "sql", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                return;
            }

            // Get SQL from statement - sqlite3_sql returns the original SQL
            const char* sql = sqlite3_sql(data->handle);
            if (sql) {
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, sql));
            } else {
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->sql));
            }
        });

    /// @description Whether a row is currently available to read.
    /// @type {boolean}
    Property(
        isolate, inst, "ready", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(self);
            if (!data) {
                info.GetReturnValue().SetFalse();
                return;
            }

            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->hasRow));
        });

    // Methods
    /// @description The current row as an object keyed by column name.
    /// @signature getObject()
    /// @returns {object|boolean|null} - Row object keyed by column name, true for a zero-column row, null if no row.
    /// @throws {Error} - if a column holds a BLOB value (not supported yet)
    Method(
        isolate, proto, "getObject", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();
            auto context = isolate->GetCurrentContext();

            args.GetReturnValue().SetNull();

            auto data = Unwrap(self);
            if (!data) {
                return;
            }

            if (!data->hasRow) {
                return;
            }

            // Return cached row if available
            if (!data->cachedRow.IsEmpty()) {
                args.GetReturnValue().Set(data->cachedRow.Get(isolate));
                return;
            }

            int32_t cols = sqlite3_column_count(data->handle);
            if (cols == 0) {
                args.GetReturnValue().Set(true);
                return;
            }

            auto obj = v8::Object::New(isolate);

            for (int32_t i = 0; i < cols; i++) {
                const char* colName = sqlite3_column_name(data->handle, i);
                v8::Local<v8::Value> val;

                switch (sqlite3_column_type(data->handle, i)) {
                    case SQLITE_INTEGER:
                        // Use double for 64-bit integers to preserve precision
                        val = v8_convert::ToV8(isolate, static_cast<double>(sqlite3_column_int64(data->handle, i)));
                        break;
                    case SQLITE_FLOAT:
                        val = v8_convert::ToV8(isolate, sqlite3_column_double(data->handle, i));
                        break;
                    case SQLITE_TEXT: {
                        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(data->handle, i));
                        val = v8_convert::ToV8(isolate, text ? text : "");
                        break;
                    }
                    case SQLITE_BLOB:
                        v8_error::ThrowError(isolate, "Blob type not supported (yet)");
                        return;
                    case SQLITE_NULL:
                    default:
                        val = v8::Null(isolate);
                        break;
                }

                obj->Set(context, v8_convert::ToV8(isolate, colName), val).Check();
            }

            data->cachedRow.Reset(isolate, obj);
            args.GetReturnValue().Set(obj);
        });

    /// @description The number of columns in the current row.
    /// @signature getColumnCount()
    /// @returns {number} - Count of columns in the current row.
    /// @throws {Error} - if the statement is not ready (no current row)
    Method(
        isolate, proto, "getColumnCount", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data || !data->hasRow) {
                v8_error::ThrowError(isolate, "Statement is not ready");
                return;
            }

            args.GetReturnValue().Set(v8_convert::ToV8(isolate, sqlite3_column_count(data->handle)));
        });

    /// @description The name of the column at the given index in the current row.
    /// @signature getColumnName(index: number)
    /// @param index {number} - Zero-based column index; must be in [0, columnCount).
    /// @returns {string} - The column's name, or empty string if unavailable.
    /// @throws {Error} - if the statement is not ready (no current row)
    /// @throws {RangeError} - if index is outside [0, columnCount)
    Method(
        isolate, proto, "getColumnName", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            if (!v8_error::CheckArgCount(args, 1, "getColumnName")) {
                return;
            }

            if (!args[0]->IsNumber()) {
                v8_error::ThrowTypeError(isolate, "getColumnName() requires column index");
                return;
            }

            auto data = Unwrap(self);
            if (!data || !data->hasRow) {
                v8_error::ThrowError(isolate, "Statement is not ready");
                return;
            }

            int32_t index = v8_convert::ToInt32(isolate, args[0]);
            if (index < 0 || index >= sqlite3_column_count(data->handle)) {
                v8_error::ThrowRangeError(isolate, "Column index out of range");
                return;
            }
            const char* name = sqlite3_column_name(data->handle, index);

            args.GetReturnValue().Set(v8_convert::ToV8(isolate, name ? name : ""));
        });

    /// @description The value of the column at the given index in the current row.
    /// @signature getColumnValue(index: number)
    /// @param index {number} - Zero-based column index; must be in [0, columnCount).
    /// @returns {number|string|null} - The column value by type, null for SQL NULL.
    /// @throws {Error} - if the statement is not ready (no current row)
    /// @throws {RangeError} - if index is outside [0, columnCount)
    /// @throws {Error} - if the column holds a BLOB value (not supported yet)
    Method(
        isolate, proto, "getColumnValue", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            if (!v8_error::CheckArgCount(args, 1, "getColumnValue")) {
                return;
            }

            if (!args[0]->IsNumber()) {
                v8_error::ThrowTypeError(isolate, "getColumnValue() requires column index");
                return;
            }

            auto data = Unwrap(self);
            if (!data || !data->hasRow) {
                v8_error::ThrowError(isolate, "Statement is not ready");
                return;
            }

            int32_t index = v8_convert::ToInt32(isolate, args[0]);
            if (index < 0 || index >= sqlite3_column_count(data->handle)) {
                v8_error::ThrowRangeError(isolate, "Column index out of range");
                return;
            }

            switch (sqlite3_column_type(data->handle, index)) {
                case SQLITE_INTEGER:
                    args.GetReturnValue().Set(
                        v8_convert::ToV8(isolate, static_cast<double>(sqlite3_column_int64(data->handle, index))));
                    break;
                case SQLITE_FLOAT:
                    args.GetReturnValue().Set(v8_convert::ToV8(isolate, sqlite3_column_double(data->handle, index)));
                    break;
                case SQLITE_TEXT: {
                    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(data->handle, index));
                    args.GetReturnValue().Set(v8_convert::ToV8(isolate, text ? text : ""));
                    break;
                }
                case SQLITE_BLOB:
                    v8_error::ThrowError(isolate, "Blob type not supported (yet)");
                    return;
                case SQLITE_NULL:
                default:
                    args.GetReturnValue().SetNull();
                    break;
            }
        });

    /// @description Executes a non-SELECT statement once, then finalizes it.
    /// @signature go()
    /// @returns {boolean} - True if the statement ran to completion, false if it returned a row.
    /// @throws {Error} - if stepping the statement fails
    Method(
        isolate, proto, "go", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                v8_error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            int32_t res = sqlite3_step(data->handle);

            if (res != SQLITE_ROW && res != SQLITE_DONE) {
                if (data->parent && data->parent->handle) {
                    v8_error::ThrowError(isolate, sqlite3_errmsg(data->parent->handle));
                } else {
                    v8_error::ThrowError(isolate, "SQLite error");
                }
                return;
            }

            // go() is for non-SELECT statements - finalize after execution
            data->Finalize();

            args.GetReturnValue().Set(res == SQLITE_DONE);
        });

    /// @description Advances the statement to the next result row.
    /// @signature next()
    /// @returns {boolean} - True if a row is now available, false if no more rows.
    /// @throws {Error} - if stepping the statement fails
    Method(
        isolate, proto, "next", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                v8_error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            int32_t res = sqlite3_step(data->handle);

            if (res != SQLITE_ROW && res != SQLITE_DONE) {
                if (data->parent && data->parent->handle) {
                    v8_error::ThrowError(isolate, sqlite3_errmsg(data->parent->handle));
                } else {
                    v8_error::ThrowError(isolate, "SQLite error");
                }
                return;
            }

            data->hasRow = (res == SQLITE_ROW);

            // Clear cached row
            data->cachedRow.Reset();

            args.GetReturnValue().Set(res == SQLITE_ROW);
        });

    /// @description Advances the statement past up to count result rows.
    /// @signature skip(count: number)
    /// @param count {number} - Maximum number of rows to skip.
    /// @returns {number} - Count of rows actually skipped, which may be less than count.
    /// @throws {Error} - if stepping the statement fails
    Method(
        isolate, proto, "skip", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            if (!v8_error::CheckArgCount(args, 1, "skip")) {
                return;
            }

            if (!args[0]->IsNumber()) {
                v8_error::ThrowTypeError(isolate, "skip() requires a count argument");
                return;
            }

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                v8_error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            int32_t count = v8_convert::ToInt32(isolate, args[0]);
            int32_t skipped = 0;

            for (int32_t i = 0; i < count; i++) {
                int32_t res = sqlite3_step(data->handle);
                if (res == SQLITE_ROW) {
                    skipped++;
                    data->hasRow = true;
                } else if (res == SQLITE_DONE) {
                    data->hasRow = false;
                    break;
                } else {
                    if (data->parent && data->parent->handle) {
                        v8_error::ThrowError(isolate, sqlite3_errmsg(data->parent->handle));
                    } else {
                        v8_error::ThrowError(isolate, "SQLite error");
                    }
                    return;
                }
            }

            // Clear cached row (stale after skipping)
            data->cachedRow.Reset();

            args.GetReturnValue().Set(v8_convert::ToV8(isolate, skipped));
        });

    /// @description Resets the statement to its initial state for re-stepping, preserving bound parameters.
    /// @signature reset()
    /// @returns {boolean} - True on successful reset.
    /// @throws {Error} - if resetting the statement fails
    Method(
        isolate, proto, "reset", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                v8_error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            if (SQLITE_OK != sqlite3_reset(data->handle)) {
                if (data->parent && data->parent->handle) {
                    v8_error::ThrowError(isolate, sqlite3_errmsg(data->parent->handle));
                } else {
                    v8_error::ThrowError(isolate, "SQLite error");
                }
                return;
            }

            data->hasRow = false;
            data->cachedRow.Reset();

            args.GetReturnValue().Set(true);
        });

    /// @description Finalizes the statement, releasing its resources and detaching it from the parent connection.
    /// @signature close()
    /// @returns {boolean} - Always true.
    Method(
        isolate, proto, "close", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data) {
                return;
            }

            data->Finalize();
            args.GetReturnValue().Set(true);
        });

    /// @description Binds a value to a statement parameter by 1-based index or by name.
    /// @signature bind(param: number, value: number|string|boolean|null)
    /// @param param {number} - 1-based parameter index.
    /// @param value {number|string|boolean|null} - Value to bind; null/undefined bind SQL NULL, integers bind INTEGER,
    /// other numbers bind REAL, strings bind TEXT, booleans bind TEXT "true"/"false".
    /// @signature bind(param: string, value: number|string|boolean|null)
    /// @param param {string} - Parameter name to resolve to an index (e.g. ":id").
    /// @param value {number|string|boolean|null} - Value to bind; null/undefined bind SQL NULL, integers bind INTEGER,
    /// other numbers bind REAL, strings bind TEXT, booleans bind TEXT "true"/"false".
    /// @returns {boolean} - Always true on success.
    /// @throws {Error} - if the parameter index/name does not resolve to a parameter (indexes start at 1)
    /// @throws {TypeError} - if value is not a bindable type (number, string, boolean, null/undefined)
    Method(
        isolate, proto, "bind", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            if (!v8_error::CheckArgCount(args, 2, "bind")) {
                return;
            }

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                v8_error::ThrowError(isolate, "Invalid statement object");
                return;
            }

            // First argument can be int32_t (column index) or string (parameter name)
            int32_t colNum = -1;

            if (args[0]->IsNumber()) {
                colNum = v8_convert::ToInt32(isolate, args[0]);
            } else if (args[0]->IsString()) {
                std::string paramName = v8_convert::ToString(isolate, args[0]);
                colNum = sqlite3_bind_parameter_index(data->handle, paramName.c_str());
            } else {
                v8_error::ThrowTypeError(isolate, "bind() requires index or parameter name");
                return;
            }

            if (colNum == 0) {
                v8_error::ThrowError(isolate, "Invalid parameter number, parameters start at 1");
                return;
            }

            // Bind based on value type
            if (!BindValue(isolate, args[1], data->handle, colNum)) {
                v8_error::ThrowTypeError(isolate, "Invalid bound parameter type");
                return;
            }

            args.GetReturnValue().Set(true);
        });
}

}  // namespace d2bs::api::classes
