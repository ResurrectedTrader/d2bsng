#include "JSSQLite.h"
#include "JSDBStatement.h"
#include "SQLiteBind.h"

#include "config/AppConfig.h"

namespace d2bs::api::classes {

static std::string PathToUtf8(const std::filesystem::path& path) {
    // In C++20+, u8string() returns std::u8string (char8_t based)
    // We need to convert it to std::string for SQLite's C API
    auto u8path = path.u8string();
    return {reinterpret_cast<const char*>(u8path.data()), u8path.size()};
}

// NOLINTNEXTLINE(bugprone-exception-escape) - std::set operations theoretically throw but won't in practice
void SQLiteData::Close() noexcept {
    if (!isOpen)
        return;

    // Take a copy of the set to avoid iterator invalidation
    // (Finalize() calls parent->statements.erase(this))
    auto stmtsCopy = statements;
    for (auto* stmt : stmtsCopy) {
        stmt->Finalize();
    }
    statements.clear();

    // Close the database
    if (handle) {
        sqlite3_close_v2(handle);
        handle = nullptr;
    }
    isOpen = false;
}

/// @description Constructs a SQLite database object, opening it immediately by default.
/// @signature SQLite(path?: string, autoOpen?: boolean)
/// @param path {string} - database file path (sandboxed) or SQLite special path (":memory:", ":..."); default
/// ":memory:" (empty string is coerced to ":memory:")
/// @param autoOpen {boolean} - if true (default) open now, if false defer to open()
/// @returns {SQLite} - the constructed database object; throws on error.
/// @throws {Error} - if path is a regular file path that escapes the script sandbox / is invalid
/// @throws {Error} - if autoOpen is true and the database cannot be opened
void JSSQLite::New(const v8::FunctionCallbackInfo<v8::Value>& args) {
    V8_CLASS_CTOR_PROLOGUE;

    auto argc = args.Length();

    std::filesystem::path path = ":memory:";  // Default to in-memory database
    bool autoOpen = true;

    // SQLite(path) or SQLite(path, autoOpen)
    if (argc > 0) {
        if (!args[0]->IsString()) {
            v8_error::ThrowTypeError(isolate, "Invalid parameters in SQLite constructor");
            return;
        }
        auto pathStr = v8_convert::ToString(isolate, args[0]);

        // Empty string in SQLite creates a temp file in the system temp directory,
        // which would bypass the script sandbox. Treat it as :memory: instead.
        if (pathStr.empty()) {
            path = ":memory:";
        } else if (pathStr[0] != ':') {
            // Regular file path - must pass sandbox validation
            auto sandboxed = config::GetPathRelScript(pathStr);
            if (sandboxed.empty()) {
                v8_error::ThrowError(isolate, "Invalid file path");
                return;
            }
            path = sandboxed;
        } else {
            // SQLite special paths (e.g. ":memory:") start with ':' and are not sandboxed
            path = pathStr;
        }

        if (argc > 1 && args[1]->IsBoolean()) {
            autoOpen = args[1]->BooleanValue(isolate);
        }
    }

    auto data = std::make_unique<SQLiteData>();
    data->path = path;

    if (autoOpen) {
        // Open database - use sqlite3_open for UTF-8 paths
        auto pathStr = PathToUtf8(path);
        if (SQLITE_OK != sqlite3_open(pathStr.c_str(), &data->handle)) {
            std::string msg = "Could not open database: ";
            msg += sqlite3_errmsg(data->handle);
            sqlite3_close(data->handle);
            v8_error::ThrowError(isolate, msg);
            return;
        }
        data->isOpen = true;
    }

    InitInstance(isolate, args.This(), std::move(data));
    args.GetReturnValue().Set(args.This());
}

void JSSQLite::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    // Properties
    /// @description The resolved database file path as a UTF-8 string.
    /// @type {string}
    Property(
        isolate, inst, "path", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(self);
            if (!data) {
                return;
            }

            info.GetReturnValue().Set(v8_convert::ToV8(isolate, PathToUtf8(data->path)));
        });

    /// @description The currently-open DBStatement objects belonging to this database.
    /// @type {DBStatement[]}
    Property(
        isolate, inst, "statements",
        +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto context = isolate->GetCurrentContext();

            auto data = Unwrap(self);
            if (!data) {
                info.GetReturnValue().Set(v8::Array::New(isolate, 0));
                return;
            }

            // Wrap as real DBStatement objects (non-owning). The native DBStatementData
            // is owned by the parent SQLiteData, not by GC. We use Wrap() without MakeWeak
            // so GC collection of a wrapper doesn't free the native data. A SetPrivate
            // reference from each wrapper to the parent SQLite JS object prevents the parent
            // from being GC'd while any statement wrapper is still reachable.
            auto array = v8::Array::New(isolate, static_cast<int32_t>(data->statements.size()));
            auto stmtTpl = JSDBStatement::GetTemplate(isolate);
            auto parentKey = v8::Private::ForApi(isolate, v8_convert::ToV8(isolate, "d2bs::DBStatement#parentDb"));
            uint32_t idx = 0;
            for (auto* stmt : data->statements) {
                if (!stmt->isOpen) {
                    continue;
                }
                auto maybeObj = stmtTpl->InstanceTemplate()->NewInstance(context);
                if (maybeObj.IsEmpty()) {
                    continue;
                }
                auto obj = maybeObj.ToLocalChecked();
                JSDBStatement::Wrap(obj, stmt);
                obj->SetPrivate(context, parentKey, self).Check();
                array->Set(context, idx++, obj).Check();
            }

            info.GetReturnValue().Set(array);
        });

    /// @description Whether the database connection is currently open.
    /// @type {boolean}
    Property(
        isolate, inst, "isOpen", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(self);
            if (!data) {
                info.GetReturnValue().SetFalse();
                return;
            }

            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->isOpen));
        });

    /// @description The rowid of the most recently inserted row on this connection.
    /// @type {number}
    Property(
        isolate, inst, "lastRowId", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                info.GetReturnValue().Set(0);
                return;
            }

            auto rowId = sqlite3_last_insert_rowid(data->handle);
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(rowId)));
        });

    /// @description The number of rows changed by the most recent statement on this connection.
    /// @type {number}
    Property(
        isolate, inst, "changes", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();

            auto data = Unwrap(self);
            if (!data || !data->handle) {
                info.GetReturnValue().Set(0);
                return;
            }

            info.GetReturnValue().Set(v8_convert::ToV8(isolate, sqlite3_changes(data->handle)));
        });

    // Instance Methods
    /// @description Executes one or more SQL statements that return no result set.
    /// @signature execute(sql: string)
    /// @param sql {string} - the SQL to execute (may contain multiple statements)
    /// @returns {boolean} - true on success; throws on error.
    /// @throws {Error} - if the database is not open
    /// @throws {Error} - if executing the SQL fails
    Method(
        isolate, proto, "execute", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            if (!v8_error::CheckArgCount(args, 1, "execute")) {
                return;
            }

            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "execute() requires a SQL string argument");
                return;
            }

            auto data = Unwrap(self);
            if (!data) {
                v8_error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (!data->isOpen) {
                v8_error::ThrowError(isolate, "Database must first be opened!");
                return;
            }

            std::string sql = v8_convert::ToString(isolate, args[0]);
            char* errMsg = nullptr;

            if (SQLITE_OK != sqlite3_exec(data->handle, sql.c_str(), nullptr, nullptr, &errMsg)) {
                std::string msg = errMsg ? errMsg : "Unknown error";
                sqlite3_free(errMsg);
                v8_error::ThrowError(isolate, msg);
                return;
            }

            args.GetReturnValue().Set(true);
        });

    /// @description Prepares a SQL query and returns a DBStatement for iterating its result set.
    /// @signature query(sql: string, ...params: (null|undefined|string|number|boolean)[])
    /// @param sql {string} - the SQL query (may contain ? / named placeholders)
    /// @param params {null|undefined|string|number|boolean} - Values to bind, by JS type.
    /// @returns {DBStatement} - a statement for stepping through results; throws on error.
    /// @throws {Error} - if the database is not open
    /// @throws {Error} - if preparing the SQL fails or the statement has no effect
    /// @throws {Error} - if a bound parameter value is unsupported / cannot be bound
    Method(
        isolate, proto, "query", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();
            auto context = isolate->GetCurrentContext();

            if (!v8_error::CheckArgCount(args, 1, "query")) {
                return;
            }

            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "query() requires a SQL string argument");
                return;
            }

            auto data = Unwrap(self);
            if (!data) {
                v8_error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (!data->isOpen) {
                v8_error::ThrowError(isolate, "Database must first be opened!");
                return;
            }

            std::string sql = v8_convert::ToString(isolate, args[0]);

            // Prepare statement
            sqlite3_stmt* stmtHandle = nullptr;
            if (SQLITE_OK !=
                sqlite3_prepare_v2(data->handle, sql.c_str(), static_cast<int>(sql.length()), &stmtHandle, nullptr)) {
                v8_error::ThrowError(isolate, sqlite3_errmsg(data->handle));
                return;
            }

            if (!stmtHandle) {
                v8_error::ThrowError(isolate, "Statement has no effect");
                return;
            }

            // Bind any additional parameters (args[1], args[2], ...). Parameters
            // are 1-indexed in SQLite; args[i] maps to paramIdx = i.
            for (int32_t i = 1; i < args.Length(); i++) {
                if (!BindValue(isolate, args[i], stmtHandle, i)) {
                    sqlite3_finalize(stmtHandle);
                    std::string msg = "Invalid bound parameter " + std::to_string(i);
                    v8_error::ThrowError(isolate, msg);
                    return;
                }
            }

            // Create DBStatementData
            auto stmtData = std::make_unique<DBStatementData>();
            stmtData->handle = stmtHandle;
            stmtData->parent = data;
            stmtData->sql = sql;
            stmtData->isOpen = true;

            // Track statement in parent database
            auto* stmtRaw = stmtData.get();
            data->statements.insert(stmtRaw);

            // Create and return DBStatement object
            auto stmtObj = JSDBStatement::CreateInstance(isolate, context, std::move(stmtData));
            if (stmtObj.IsEmpty()) {
                data->statements.erase(stmtRaw);
                return;
            }
            args.GetReturnValue().Set(stmtObj);
        });

    /// @description Opens the database connection, a no-op if already open.
    /// @signature open()
    /// @returns {boolean} - always true on success; throws on open failure
    /// @throws {Error} - if the database cannot be opened
    Method(
        isolate, proto, "open", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data) {
                v8_error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (!data->isOpen) {
                auto pathStr = PathToUtf8(data->path);
                if (SQLITE_OK != sqlite3_open(pathStr.c_str(), &data->handle)) {
                    std::string msg = "Could not open database: ";
                    msg += sqlite3_errmsg(data->handle);
                    sqlite3_close(data->handle);
                    data->handle = nullptr;
                    v8_error::ThrowError(isolate, msg);
                    return;
                }
                data->isOpen = true;
            }

            args.GetReturnValue().Set(true);
        });

    /// @description Closes the database connection, finalizing its open statements first; a no-op if already closed.
    /// @signature close()
    /// @returns {boolean} - always true on success; throws on close error
    /// @throws {Error} - if closing the database fails
    Method(
        isolate, proto, "close", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto self = args.This();

            auto data = Unwrap(self);
            if (!data) {
                v8_error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (data->isOpen) {
                // Close all statements first (copy set to avoid iterator invalidation)
                auto stmtsCopy = data->statements;
                for (auto* stmt : stmtsCopy) {
                    stmt->Finalize();
                }
                data->statements.clear();

                // Close database - use sqlite3_close_v2 which guarantees eventual cleanup
                // even if statements are still busy
                auto rc = sqlite3_close_v2(data->handle);
                if (rc != SQLITE_OK) {
                    std::string msg = "Could not close database: ";
                    msg += sqlite3_errmsg(data->handle);
                    v8_error::ThrowError(isolate, msg);
                    // sqlite3_close_v2 marks the connection for deferred close,
                    // so we still clear our state to avoid double-close
                }
                data->handle = nullptr;
                data->isOpen = false;
            }

            args.GetReturnValue().Set(true);
        });

    // Static Methods
    /// @description Returns the SQLite library version string.
    /// @signature SQLite.version()
    /// @returns {string} - the SQLite version (e.g. "3.x.y")
    StaticMethod(
        isolate, tpl, "version", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, sqlite3_version));
        });

    /// @description Returns the number of bytes of memory currently in use by the SQLite library.
    /// @signature SQLite.memoryUsage()
    /// @returns {number} - bytes of memory currently allocated by SQLite
    StaticMethod(
        isolate, tpl, "memoryUsage", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(sqlite3_memory_used())));
        });
}

}  // namespace d2bs::api::classes
