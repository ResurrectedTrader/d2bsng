#include "JSSQLite.h"
#include "JSDBStatement.h"
#include "SQLiteBind.h"

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"

namespace d2bs::api::classes {

namespace {

std::string PathToUtf8(const std::filesystem::path& path) {
    // u8string() is std::u8string (char8_t); SQLite's C API wants char.
    auto u8path = path.u8string();
    return {reinterpret_cast<const char*>(u8path.data()), u8path.size()};
}

// Finalize every live statement of `data`. Iterates a copy: Finalize() erases the statement from
// `data.statements`.
void FinalizeStatements(SQLiteData& data) {
    auto statements = data.statements;
    for (const auto& weak : statements) {
        if (auto stmt = weak.lock()) {
            stmt->Finalize();
        }
    }
    data.statements.clear();
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape) - vector copy / weak_ptr lock theoretically throw but won't in practice
void SQLiteData::Close() noexcept {
    // Runs unconditionally, even on the !isOpen path, so staying safe does not depend on every
    // future path clearing `isOpen` only after emptying the list. A statement holds its database
    // alive, so by the time this runs from the destructor no statement is live and this is a no-op.
    FinalizeStatements(*this);

    if (!isOpen) {
        return;
    }

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
std::unique_ptr<SQLiteData> JSSQLite::New(const ub::CallbackInfo& args) {
    auto& isolate = args.GetIsolate();
    const auto& context = args.GetContext();
    auto argc = args.Length();

    std::filesystem::path path = ":memory:";
    bool autoOpen = true;

    if (argc > 0) {
        if (!args[0].IsString()) {
            error::ThrowTypeError(isolate, "Invalid parameters in SQLite constructor");
            return nullptr;
        }
        auto pathStr = convert::ToString(context, args[0]);

        // Empty string in SQLite creates a temp file in the system temp directory,
        // which would bypass the script sandbox. Treat it as :memory: instead.
        if (pathStr.empty()) {
            path = ":memory:";
        } else if (pathStr[0] != ':') {
            auto sandboxed = config::GetPathRelScript(pathStr);
            if (sandboxed.empty()) {
                error::ThrowError(isolate, "Invalid file path");
                return nullptr;
            }
            path = sandboxed;
        } else {
            // SQLite special paths (e.g. ":memory:") start with ':' and are not sandboxed
            path = pathStr;
        }

        if (argc > 1 && args[1].IsBoolean()) {
            autoOpen = args[1].IsTrue();
        }
    }

    auto data = std::make_unique<SQLiteData>();
    data->path = path;

    if (autoOpen) {
        auto pathStr = PathToUtf8(path);
        if (SQLITE_OK != sqlite3_open(pathStr.c_str(), &data->handle)) {
            std::string msg = "Could not open database: ";
            msg += sqlite3_errmsg(data->handle);
            sqlite3_close(data->handle);
            data->handle = nullptr;
            error::ThrowError(isolate, msg);
            return nullptr;
        }
        data->isOpen = true;
    }

    return data;
}

void JSSQLite::Configure(const ub::Class<SQLiteData>& cls) {
    // Properties
    /// @description The resolved database file path as a UTF-8 string.
    /// @type {string}
    Property(
        cls, "path", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            auto* data = Unwrap(info.This());
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), PathToUtf8(data->path)));
        });

    /// @description The currently-open DBStatement objects belonging to this database.
    /// @type {DBStatement[]}
    Property(
        cls, "statements", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            const auto& context = info.GetContext();

            auto* data = Unwrap(info.This());
            auto array = ub::Array::New(context, 0);
            if (!array) {
                return;
            }
            if (!data) {
                info.GetReturnValue().Set(*array);
                return;
            }

            // Each wrapper holds a share of its statement, and the statement a share of this
            // database, so a statement handed out here keeps the database alive by itself.
            uint32_t idx = 0;
            for (const auto& weak : data->statements) {
                auto stmt = weak.lock();
                if (!stmt || !stmt->isOpen) {
                    continue;
                }
                auto obj = JSDBStatement::Wrap(context, std::move(stmt));
                if (!obj) {
                    continue;
                }
                if (!array->Set(context, idx++, *obj)) {
                    return;
                }
            }

            info.GetReturnValue().Set(*array);
        });

    /// @description Whether the database connection is currently open.
    /// @type {boolean}
    Property(
        cls, "isOpen", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            auto* data = Unwrap(info.This());
            info.GetReturnValue().Set(data != nullptr && data->isOpen);
        });

    /// @description The rowid of the most recently inserted row on this connection.
    /// @type {number}
    Property(
        cls, "lastRowId", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            auto* data = Unwrap(info.This());
            if (!data || !data->handle) {
                info.GetReturnValue().Set(0);
                return;
            }
            info.GetReturnValue().Set(static_cast<double>(sqlite3_last_insert_rowid(data->handle)));
        });

    /// @description The number of rows changed by the most recent statement on this connection.
    /// @type {number}
    Property(
        cls, "changes", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
            auto* data = Unwrap(info.This());
            if (!data || !data->handle) {
                info.GetReturnValue().Set(0);
                return;
            }
            info.GetReturnValue().Set(sqlite3_changes(data->handle));
        });

    // Instance Methods
    /// @description Executes one or more SQL statements that return no result set.
    /// @signature execute(sql: string)
    /// @param sql {string} - the SQL to execute (may contain multiple statements)
    /// @returns {boolean} - true on success; throws on error.
    /// @throws {Error} - if the database is not open
    /// @throws {Error} - if executing the SQL fails
    Method(
        cls, "execute", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            if (!error::CheckArgCount(args, 1, "execute")) {
                return;
            }

            if (!args[0].IsString()) {
                error::ThrowTypeError(isolate, "execute() requires a SQL string argument");
                return;
            }

            auto* data = Unwrap(args.This());
            if (!data) {
                error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (!data->isOpen) {
                error::ThrowError(isolate, "Database must first be opened!");
                return;
            }

            std::string sql = convert::ToString(args.GetContext(), args[0]);
            char* errMsg = nullptr;

            if (SQLITE_OK != sqlite3_exec(data->handle, sql.c_str(), nullptr, nullptr, &errMsg)) {
                std::string msg = errMsg ? errMsg : "Unknown error";
                sqlite3_free(errMsg);
                error::ThrowError(isolate, msg);
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
        cls, "query", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();

            if (!error::CheckArgCount(args, 1, "query")) {
                return;
            }

            if (!args[0].IsString()) {
                error::ThrowTypeError(isolate, "query() requires a SQL string argument");
                return;
            }

            auto data = UnwrapShared(args.This());
            if (!data) {
                error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (!data->isOpen) {
                error::ThrowError(isolate, "Database must first be opened!");
                return;
            }

            std::string sql = convert::ToString(context, args[0]);

            sqlite3_stmt* stmtHandle = nullptr;
            if (SQLITE_OK !=
                sqlite3_prepare_v2(data->handle, sql.c_str(), static_cast<int>(sql.length()), &stmtHandle, nullptr)) {
                error::ThrowError(isolate, sqlite3_errmsg(data->handle));
                return;
            }

            if (!stmtHandle) {
                error::ThrowError(isolate, "Statement has no effect");
                return;
            }

            // Parameters are 1-indexed in SQLite; args[i] maps to paramIdx = i.
            for (uint32_t i = 1; i < args.Length(); i++) {
                if (!BindValue(context, args[i], stmtHandle, static_cast<int32_t>(i))) {
                    sqlite3_finalize(stmtHandle);
                    std::string msg = "Invalid bound parameter " + std::to_string(i);
                    error::ThrowError(isolate, msg);
                    return;
                }
            }

            auto stmt = std::make_shared<DBStatementData>();
            stmt->handle = stmtHandle;
            stmt->parent = data;
            stmt->sql = sql;
            stmt->isOpen = true;
            data->statements.emplace_back(stmt);

            // On failure the wrapper's share goes with it and the statement unregisters itself.
            auto stmtObj = JSDBStatement::Wrap(context, std::move(stmt));
            if (!stmtObj) {
                return;
            }
            args.GetReturnValue().Set(*stmtObj);
        });

    /// @description Opens the database connection, a no-op if already open.
    /// @signature open()
    /// @returns {boolean} - always true on success; throws on open failure
    /// @throws {Error} - if the database cannot be opened
    Method(
        cls, "open", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            auto* data = Unwrap(args.This());
            if (!data) {
                error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (!data->isOpen) {
                auto pathStr = PathToUtf8(data->path);
                if (SQLITE_OK != sqlite3_open(pathStr.c_str(), &data->handle)) {
                    std::string msg = "Could not open database: ";
                    msg += sqlite3_errmsg(data->handle);
                    sqlite3_close(data->handle);
                    data->handle = nullptr;
                    error::ThrowError(isolate, msg);
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
        cls, "close", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();

            auto* data = Unwrap(args.This());
            if (!data) {
                error::ThrowError(isolate, "Invalid SQLite object");
                return;
            }

            if (data->isOpen) {
                FinalizeStatements(*data);

                // sqlite3_close_v2 guarantees eventual cleanup even if statements are still busy
                auto rc = sqlite3_close_v2(data->handle);
                if (rc != SQLITE_OK) {
                    std::string msg = "Could not close database: ";
                    msg += sqlite3_errmsg(data->handle);
                    error::ThrowError(isolate, msg);
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
        cls, "version", +[](const ub::CallbackInfo& args) {
            args.GetReturnValue().Set(convert::ToJS(args.GetIsolate(), sqlite3_version));
        });

    /// @description Returns the number of bytes of memory currently in use by the SQLite library.
    /// @signature SQLite.memoryUsage()
    /// @returns {number} - bytes of memory currently allocated by SQLite
    StaticMethod(
        cls, "memoryUsage",
        +[](const ub::CallbackInfo& args) { args.GetReturnValue().Set(static_cast<double>(sqlite3_memory_used())); });
}

}  // namespace d2bs::api::classes
