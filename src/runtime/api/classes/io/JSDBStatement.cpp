#include "JSDBStatement.h"
#include "JSSQLite.h"
#include "SQLiteBind.h"

#include <cstring>
#include <string_view>
#include <utility>

namespace d2bs::runtime::api::classes {

namespace {

// Thrown by every method that needs a live statement after close(), go() or the database's close().
constexpr std::string_view STMT_CLOSED = "The statement is closed";

v8::Local<v8::ArrayBuffer> ColumnBlob(v8::Isolate* isolate, sqlite3_stmt* handle, int32_t index) {
    // SQLite documents this order: the byte count describes the value the
    // blob call returned.
    const void* bytes = sqlite3_column_blob(handle, index);
    const auto size = static_cast<size_t>(sqlite3_column_bytes(handle, index));
    auto buffer = v8::ArrayBuffer::New(isolate, size);
    if (bytes != nullptr && size != 0) {
        std::memcpy(buffer->Data(), bytes, size);
    }
    return buffer;
}

v8::Local<v8::Value> ColumnValue(v8::Isolate* isolate, sqlite3_stmt* handle, int32_t index) {
    switch (sqlite3_column_type(handle, index)) {
        case SQLITE_INTEGER:
            // Use double for 64-bit integers to preserve precision
            return convert::ToJS(isolate, static_cast<double>(sqlite3_column_int64(handle, index)));
        case SQLITE_FLOAT:
            return convert::ToJS(isolate, sqlite3_column_double(handle, index));
        case SQLITE_TEXT: {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(handle, index));
            return convert::ToJS(isolate, text ? text : "");
        }
        case SQLITE_BLOB:
            return ColumnBlob(isolate, handle, index);
        case SQLITE_NULL:
        default:
            return v8::Null(isolate);
    }
}

// The step that failed, reported with the database's message.
void ThrowStepError(v8::Isolate* isolate, const DBStatementData& data) {
    if (data.parent && data.parent->handle) {
        error::ThrowError(isolate, sqlite3_errmsg(data.parent->handle));
    } else {
        error::ThrowError(isolate, STMT_CLOSED);
    }
}

// A step leaves the current row whatever it returns, so the cached row and ready state go first.
void DropRow(DBStatementData& data) {
    data.hasRow = false;
    data.cachedRow.Reset();
}

// The reference's checks for getColumnName / getColumnValue, in its order: a closed statement, no current row, then
// the index. False with an Error pending.
bool CheckColumnArgs(const v8::FunctionCallbackInfo<v8::Value>& args, const DBStatementData& data,
                     std::string_view method, int32_t& index) {
    auto* isolate = args.GetIsolate();
    if (!data.handle) {
        error::ThrowError(isolate, STMT_CLOSED);
        return false;
    }
    if (!data.hasRow) {
        error::ThrowError(isolate, "Statement is not ready");
        return false;
    }
    if (args.Length() != 1 || !args[0]->IsInt32()) {
        error::ThrowError(isolate, std::string("Invalid parameter for SQLiteStatement.").append(method));
        return false;
    }
    index = args[0].As<v8::Int32>()->Value();
    if (index < 0 || index >= sqlite3_column_count(data.handle)) {
        error::ThrowError(isolate, "Invalid column index");
        return false;
    }
    return true;
}

}  // namespace

DBStatementData::DBStatementData(sqlite3_stmt* stmt, SQLiteData* db, std::string text)
    : handle(stmt), parent(db), sql(std::move(text)), isOpen(true) {
    parent->statements.insert(this);
}

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

    hasRow = false;
}

void DBStatementData::Close() {
    Finalize();
    cachedRow.Reset();
    parentObject.Reset();
}

// Every method keeps the reference's three answers to an unusable statement: a `this` that is no DBStatement (a
// foreign object or the prototype) gets a neutral value, a closed statement throws "The statement is closed", and
// reading the row with no current row throws "Statement is not ready".
// NOLINTNEXTLINE(readability-function-size) - V8 template configuration, intentionally large
void JSDBStatement::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    // Properties
    /// @description The SQL text of the prepared statement; undefined once it is closed.
    /// @type {string}
    Property(
        isolate, inst, "sql", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(isolate, self);
            if (!data || !data->handle) {
                return;
            }

            // Get SQL from statement - sqlite3_sql returns the original SQL
            const char* sql = sqlite3_sql(data->handle);
            if (sql) {
                info.GetReturnValue().Set(convert::ToJS(isolate, sql));
            } else {
                info.GetReturnValue().Set(convert::ToJS(isolate, data->sql));
            }
        });

    /// @description Whether a row is currently available to read; false once the statement is closed.
    /// @type {boolean}
    Property(
        isolate, inst, "ready", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(isolate, self);
            if (!data) {
                info.GetReturnValue().SetFalse();
                return;
            }

            info.GetReturnValue().Set(convert::ToJS(isolate, data->hasRow));
        });

    // Methods
    /// @description The current row as an object keyed by column name; the same object until the statement steps.
    /// @signature getObject()
    /// @returns {object|boolean|null} - Row object keyed by column name (BLOB columns as an ArrayBuffer), true for a
    /// zero-column row, null if no row.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    Method(
        isolate, proto, "getObject", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();
            auto context = isolate->GetCurrentContext();

            args.GetReturnValue().SetNull();

            auto data = Unwrap(isolate, self);
            if (!data) {
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
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
                // Defined, not set: an inherited setter for the name would run script mid-row.
                obj->CreateDataProperty(context, convert::ToJS(isolate, colName ? colName : ""),
                                        ColumnValue(isolate, data->handle, i))
                    .Check();
            }

            data->cachedRow.Reset(isolate, obj);
            args.GetReturnValue().Set(obj);
        });

    /// @description The number of columns in the current row.
    /// @signature getColumnCount()
    /// @returns {number} - Count of columns in the current row; 0 when this is not a DBStatement.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - if the statement is not ready (no current row)
    Method(
        isolate, proto, "getColumnCount", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                args.GetReturnValue().Set(0);
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
                return;
            }
            if (!data->hasRow) {
                error::ThrowError(isolate, "Statement is not ready");
                return;
            }

            args.GetReturnValue().Set(convert::ToJS(isolate, sqlite3_column_count(data->handle)));
        });

    /// @description The name of the column at the given index in the current row.
    /// @signature getColumnName(index: number)
    /// @param index {number} - Zero-based column index; must be an integer in [0, columnCount).
    /// @returns {string} - The column's name; undefined when this is not a DBStatement.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - if the statement is not ready (no current row)
    /// @throws {Error} - index is missing or not an integer ("Invalid parameter for SQLiteStatement.getColumnName")
    /// @throws {Error} - index is outside [0, columnCount) ("Invalid column index")
    Method(
        isolate, proto, "getColumnName", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                return;
            }
            int32_t index = 0;
            if (!CheckColumnArgs(args, *data, "getColumnName", index)) {
                return;
            }
            const char* name = sqlite3_column_name(data->handle, index);
            args.GetReturnValue().Set(convert::ToJS(isolate, name ? name : ""));
        });

    /// @description The value of the column at the given index in the current row.
    /// @signature getColumnValue(index: number)
    /// @param index {number} - Zero-based column index; must be an integer in [0, columnCount).
    /// @returns {number|string|ArrayBuffer|null} - The column value by type (a BLOB as an ArrayBuffer), null for SQL
    /// NULL; undefined when this is not a DBStatement.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - if the statement is not ready (no current row)
    /// @throws {Error} - index is missing or not an integer ("Invalid parameter for SQLiteStatement.getColumnValue")
    /// @throws {Error} - index is outside [0, columnCount) ("Invalid column index")
    Method(
        isolate, proto, "getColumnValue", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                return;
            }
            int32_t index = 0;
            if (!CheckColumnArgs(args, *data, "getColumnValue", index)) {
                return;
            }
            args.GetReturnValue().Set(ColumnValue(isolate, data->handle, index));
        });

    /// @description Executes a non-SELECT statement once, then closes it.
    /// @signature go()
    /// @returns {boolean} - True if the statement ran to completion, false if it returned a row (or this is not a
    /// DBStatement).
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - if stepping the statement fails; the statement stays open and not ready
    Method(
        isolate, proto, "go", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
                return;
            }

            DropRow(*data);
            int32_t res = sqlite3_step(data->handle);
            if (res != SQLITE_ROW && res != SQLITE_DONE) {
                ThrowStepError(isolate, *data);
                return;
            }

            // go() is for non-SELECT statements - finalize after execution
            data->Close();

            args.GetReturnValue().Set(res == SQLITE_DONE);
        });

    /// @description Advances the statement to the next result row.
    /// @signature next()
    /// @returns {boolean} - True if a row is now available, false if no more rows (or this is not a DBStatement).
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - if stepping the statement fails
    Method(
        isolate, proto, "next", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
                return;
            }

            DropRow(*data);
            int32_t res = sqlite3_step(data->handle);
            if (res != SQLITE_ROW && res != SQLITE_DONE) {
                ThrowStepError(isolate, *data);
                return;
            }

            data->hasRow = (res == SQLITE_ROW);
            args.GetReturnValue().Set(res == SQLITE_ROW);
        });

    /// @description Advances the statement past up to count result rows, stopping at the end of the results.
    /// @signature skip(count: number)
    /// @param count {number} - Maximum number of rows to skip, an integer; 0 or less skips nothing.
    /// @returns {number} - Count of rows actually skipped, which may be less than count; 0 when this is not a
    /// DBStatement.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - count is missing or not an integer ("Invalid parameter to SQLiteStatement.skip")
    /// @throws {Error} - if stepping the statement fails
    Method(
        isolate, proto, "skip", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                args.GetReturnValue().Set(0);
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
                return;
            }
            if (args.Length() < 1 || !args[0]->IsInt32()) {
                error::ThrowError(isolate, "Invalid parameter to SQLiteStatement.skip");
                return;
            }

            const int32_t count = args[0].As<v8::Int32>()->Value();
            int32_t skipped = 0;
            if (count > 0) {
                data->cachedRow.Reset();
                // Stops at the end of the results: a further step would restart the statement at its first row.
                for (; skipped < count; skipped++) {
                    const int32_t res = sqlite3_step(data->handle);
                    if (res == SQLITE_ROW) {
                        data->hasRow = true;
                        continue;
                    }
                    data->hasRow = false;
                    if (res == SQLITE_DONE) {
                        break;
                    }
                    ThrowStepError(isolate, *data);
                    return;
                }
            }

            args.GetReturnValue().Set(convert::ToJS(isolate, skipped));
        });

    /// @description Resets the statement to its initial state for re-stepping, preserving bound parameters.
    /// @signature reset()
    /// @returns {boolean} - True on successful reset; false when this is not a DBStatement.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - if resetting the statement fails (it reports the last step's error, and is rewound anyway)
    Method(
        isolate, proto, "reset", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
                return;
            }

            // sqlite3_reset rewinds the statement even when it reports the last step's error.
            const int32_t res = sqlite3_reset(data->handle);
            DropRow(*data);
            if (res != SQLITE_OK) {
                ThrowStepError(isolate, *data);
                return;
            }

            args.GetReturnValue().Set(true);
        });

    /// @description Finalizes the statement, releasing its resources and detaching it from the parent connection.
    /// Closing again does nothing.
    /// @signature close()
    /// @returns {boolean} - True; false when this is not a DBStatement.
    Method(
        isolate, proto, "close", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto data = Unwrap(args.GetIsolate(), args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            data->Close();
            args.GetReturnValue().Set(true);
        });

    /// @description Binds a value to a statement parameter by 1-based index or by name.
    /// @signature bind(param: number, value: number|string|boolean|ArrayBuffer|ArrayBufferView|null)
    /// @param param {number} - 1-based parameter index.
    /// @param value {number|string|boolean|ArrayBuffer|ArrayBufferView|null} - Value to bind; null/undefined bind SQL
    /// NULL, integers bind INTEGER, other numbers bind REAL, strings bind TEXT, booleans bind INTEGER 1/0, an
    /// ArrayBuffer or typed array / DataView binds a BLOB of its bytes.
    /// @signature bind(param: string, value: number|string|boolean|ArrayBuffer|ArrayBufferView|null)
    /// @param param {string} - Parameter name to resolve to an index (e.g. ":id").
    /// @param value {number|string|boolean|ArrayBuffer|ArrayBufferView|null} - Value to bind; null/undefined bind SQL
    /// NULL, integers bind INTEGER, other numbers bind REAL, strings bind TEXT, booleans bind INTEGER 1/0, an
    /// ArrayBuffer or typed array / DataView binds a BLOB of its bytes.
    /// @returns {boolean} - True on success; false when this is not a DBStatement.
    /// @throws {Error} - the statement is closed ("The statement is closed")
    /// @throws {Error} - not exactly two arguments, or param is neither an integer nor a string ("Invalid parameters
    /// for SQLiteStatement.bind")
    /// @throws {Error} - the parameter name does not resolve, or the index is 0 (indexes start at 1)
    /// @throws {Error} - value is not a bindable type ("Invalid bound parameter")
    /// @throws {Error} - SQLite refuses the binding, such as an index past the last parameter or a statement that has
    /// stepped and not been reset ("Could not bind parameter N: ...")
    Method(
        isolate, proto, "bind", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(isolate, args.This());
            if (!data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            if (!data->handle) {
                error::ThrowError(isolate, STMT_CLOSED);
                return;
            }
            if (args.Length() != 2 || !(args[0]->IsInt32() || args[0]->IsString())) {
                error::ThrowError(isolate, "Invalid parameters for SQLiteStatement.bind");
                return;
            }

            // First argument can be int32_t (column index) or string (parameter name)
            int32_t colNum = -1;
            if (args[0]->IsInt32()) {
                colNum = args[0].As<v8::Int32>()->Value();
            } else {
                std::string paramName = convert::ToString(isolate, args[0]);
                colNum = sqlite3_bind_parameter_index(data->handle, paramName.c_str());
            }

            if (colNum == 0) {
                error::ThrowError(isolate, "Invalid parameter number, parameters start at 1");
                return;
            }

            switch (BindValue(isolate, args[1], data->handle, colNum)) {
                case BindResult::Bound:
                    args.GetReturnValue().Set(true);
                    return;
                case BindResult::BadType:
                    error::ThrowError(isolate, "Invalid bound parameter");
                    return;
                case BindResult::Failed:
                    return;
            }
        });
}

}  // namespace d2bs::runtime::api::classes
