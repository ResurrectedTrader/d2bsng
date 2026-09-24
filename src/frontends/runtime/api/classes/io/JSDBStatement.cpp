#include "JSDBStatement.h"
#include "JSSQLite.h"
#include "SQLiteBind.h"

#include <memory>
#include <optional>
#include <vector>

#include "api/core/Convert.h"
#include "api/core/Error.h"

namespace d2bs::api::classes {

namespace {

void ThrowSqliteError(ub::Isolate& isolate, const DBStatementData& data) {
    if (data.parent && data.parent->handle) {
        error::ThrowError(isolate, sqlite3_errmsg(data.parent->handle));
    } else {
        error::ThrowError(isolate, "SQLite error");
    }
}

// A column of the current row as a script value; empty after throwing for a BLOB.
std::optional<ub::Local<ub::Value>> ColumnValue(ub::Isolate& isolate, sqlite3_stmt* handle, int32_t index) {
    switch (sqlite3_column_type(handle, index)) {
        case SQLITE_INTEGER:
            // Double rather than int32 so 64-bit integers keep what precision they can
            return convert::ToJS(isolate, static_cast<double>(sqlite3_column_int64(handle, index)));
        case SQLITE_FLOAT:
            return convert::ToJS(isolate, sqlite3_column_double(handle, index));
        case SQLITE_TEXT: {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(handle, index));
            auto str = ub::String::NewFromUtf8(isolate, text ? text : "");
            if (!str) {
                return std::nullopt;
            }
            return *str;
        }
        case SQLITE_BLOB:
            error::ThrowError(isolate, "Blob type not supported (yet)");
            return std::nullopt;
        case SQLITE_NULL:
        default:
            return ub::Null(isolate);
    }
}

}  // namespace

void DBStatementData::Finalize() {
    if (handle && isOpen) {
        sqlite3_finalize(handle);
        handle = nullptr;
        isOpen = false;
    }

    if (parent) {
        // Also drops entries whose statement is already gone - including this one when called
        // from the destructor, where its own weak_ptr has already expired.
        std::erase_if(parent->statements, [this](const std::weak_ptr<DBStatementData>& weak) {
            auto stmt = weak.lock();
            return !stmt || stmt.get() == this;
        });
        // Released last: this may be the database's final share.
        parent.reset();
    }

    cachedRow.Reset();
    hasRow = false;
}

void JSDBStatement::Configure(const ub::Class<DBStatementData>& cls) {
    // Properties
    /// @description The SQL text of the prepared statement.
    /// @type {string}
    Property(
        cls, "sql", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            auto* data = Unwrap(info.This());
            if (!data || !data->handle) {
                return;
            }

            // sqlite3_sql returns the original SQL
            const char* sql = sqlite3_sql(data->handle);
            if (sql) {
                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), sql));
            } else {
                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), data->sql));
            }
        });

    /// @description Whether a row is currently available to read.
    /// @type {boolean}
    Property(
        cls, "ready", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            auto* data = Unwrap(info.This());
            info.GetReturnValue().Set(data != nullptr && data->hasRow);
        });

    // Methods
    /// @description The current row as an object keyed by column name.
    /// @signature getObject()
    /// @returns {object|boolean|null} - Row object keyed by column name, true for a zero-column row, null if no row.
    /// @throws {Error} - if a column holds a BLOB value (not supported yet)
    Method(
        cls, "getObject", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();

            args.GetReturnValue().SetNull();

            auto* data = Unwrap(args.This());
            if (!data || !data->hasRow) {
                return;
            }

            if (!data->cachedRow.IsEmpty()) {
                args.GetReturnValue().Set(data->cachedRow);
                return;
            }

            int32_t cols = sqlite3_column_count(data->handle);
            if (cols == 0) {
                args.GetReturnValue().Set(true);
                return;
            }

            auto obj = ub::Object::New(context);
            if (!obj) {
                return;
            }

            for (int32_t i = 0; i < cols; i++) {
                const char* colName = sqlite3_column_name(data->handle, i);
                auto val = ColumnValue(isolate, data->handle, i);
                if (!val) {
                    return;
                }
                if (!obj->Set(context, convert::ToJS(isolate, colName), *val)) {
                    return;
                }
            }

            data->cachedRow = ub::Global<ub::Object>(isolate, *obj);
            args.GetReturnValue().Set(*obj);
        });

    /// @description The number of columns in the current row.
    /// @signature getColumnCount()
    /// @returns {number} - Count of columns in the current row.
    /// @throws {Error} - if the statement is not ready (no current row)
    Method(
        cls, "getColumnCount", +[](const ub::CallbackInfo& args) {
            auto* data = Unwrap(args.This());
            if (!data || !data->hasRow) {
                error::ThrowError(args.GetIsolate(), "Statement is not ready");
                return;
            }

            args.GetReturnValue().Set(sqlite3_column_count(data->handle));
        });

    /// @description The name of the column at the given index in the current row.
    /// @signature getColumnName(index: number)
    /// @param index {number} - Zero-based column index; must be in [0, columnCount).
    /// @returns {string} - The column's name, or empty string if unavailable.
    /// @throws {Error} - if the statement is not ready (no current row)
    /// @throws {RangeError} - if index is outside [0, columnCount)
    Method(
        cls, "getColumnName", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "getColumnName")) {
                return;
            }

            if (!args[0].IsNumber()) {
                error::ThrowTypeError(isolate, "getColumnName() requires column index");
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data || !data->hasRow) {
                error::ThrowError(isolate, "Statement is not ready");
                return;
            }

            int32_t index = convert::ToInt32(args.GetContext(), args[0]);
            if (index < 0 || index >= sqlite3_column_count(data->handle)) {
                error::ThrowRangeError(isolate, "Column index out of range");
                return;
            }
            const char* name = sqlite3_column_name(data->handle, index);

            args.GetReturnValue().Set(convert::ToJS(isolate, name ? name : ""));
        });

    /// @description The value of the column at the given index in the current row.
    /// @signature getColumnValue(index: number)
    /// @param index {number} - Zero-based column index; must be in [0, columnCount).
    /// @returns {number|string|null} - The column value by type, null for SQL NULL.
    /// @throws {Error} - if the statement is not ready (no current row)
    /// @throws {RangeError} - if index is outside [0, columnCount)
    /// @throws {Error} - if the column holds a BLOB value (not supported yet)
    Method(
        cls, "getColumnValue", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "getColumnValue")) {
                return;
            }

            if (!args[0].IsNumber()) {
                error::ThrowTypeError(isolate, "getColumnValue() requires column index");
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data || !data->hasRow) {
                error::ThrowError(isolate, "Statement is not ready");
                return;
            }

            int32_t index = convert::ToInt32(args.GetContext(), args[0]);
            if (index < 0 || index >= sqlite3_column_count(data->handle)) {
                error::ThrowRangeError(isolate, "Column index out of range");
                return;
            }

            if (auto val = ColumnValue(isolate, data->handle, index)) {
                args.GetReturnValue().Set(*val);
            }
        });

    /// @description Executes a non-SELECT statement once, then finalizes it.
    /// @signature go()
    /// @returns {boolean} - True if the statement ran to completion, false if it returned a row.
    /// @throws {Error} - if stepping the statement fails
    Method(
        cls, "go", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            auto* data = Unwrap(args.This());
            if (!data || !data->handle) {
                error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            int32_t res = sqlite3_step(data->handle);

            if (res != SQLITE_ROW && res != SQLITE_DONE) {
                ThrowSqliteError(isolate, *data);
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
        cls, "next", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            auto* data = Unwrap(args.This());
            if (!data || !data->handle) {
                error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            int32_t res = sqlite3_step(data->handle);

            if (res != SQLITE_ROW && res != SQLITE_DONE) {
                ThrowSqliteError(isolate, *data);
                return;
            }

            data->hasRow = (res == SQLITE_ROW);
            data->cachedRow.Reset();

            args.GetReturnValue().Set(res == SQLITE_ROW);
        });

    /// @description Advances the statement past up to count result rows.
    /// @signature skip(count: number)
    /// @param count {number} - Maximum number of rows to skip.
    /// @returns {number} - Count of rows actually skipped, which may be less than count.
    /// @throws {Error} - if stepping the statement fails
    Method(
        cls, "skip", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "skip")) {
                return;
            }

            if (!args[0].IsNumber()) {
                error::ThrowTypeError(isolate, "skip() requires a count argument");
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data || !data->handle) {
                error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            int32_t count = convert::ToInt32(args.GetContext(), args[0]);
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
                    ThrowSqliteError(isolate, *data);
                    return;
                }
            }

            data->cachedRow.Reset();

            args.GetReturnValue().Set(skipped);
        });

    /// @description Resets the statement to its initial state for re-stepping, preserving bound parameters.
    /// @signature reset()
    /// @returns {boolean} - True on successful reset.
    /// @throws {Error} - if resetting the statement fails
    Method(
        cls, "reset", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            auto* data = Unwrap(args.This());
            if (!data || !data->handle) {
                error::ThrowError(isolate, "Invalid or finalized statement object");
                return;
            }

            if (SQLITE_OK != sqlite3_reset(data->handle)) {
                ThrowSqliteError(isolate, *data);
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
        cls, "close", +[](const ub::CallbackInfo& args) {
            auto* data = Unwrap(args.This());
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
        cls, "bind", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();

            if (!error::CheckArgCount(args, 2, "bind")) {
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data || !data->handle) {
                error::ThrowError(isolate, "Invalid statement object");
                return;
            }

            // First argument is the parameter index or the parameter name
            int32_t colNum = -1;

            if (args[0].IsNumber()) {
                colNum = convert::ToInt32(context, args[0]);
            } else if (args[0].IsString()) {
                std::string paramName = convert::ToString(context, args[0]);
                colNum = sqlite3_bind_parameter_index(data->handle, paramName.c_str());
            } else {
                error::ThrowTypeError(isolate, "bind() requires index or parameter name");
                return;
            }

            if (colNum == 0) {
                error::ThrowError(isolate, "Invalid parameter number, parameters start at 1");
                return;
            }

            if (!BindValue(context, args[1], data->handle, colNum)) {
                error::ThrowTypeError(isolate, "Invalid bound parameter type");
                return;
            }

            args.GetReturnValue().Set(true);
        });
}

}  // namespace d2bs::api::classes
