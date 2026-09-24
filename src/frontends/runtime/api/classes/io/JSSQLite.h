#pragma once

#include <sqlite3.h>
#include <filesystem>
#include <memory>
#include <vector>

#include "api/core/Class.h"

namespace d2bs::api::classes {

struct DBStatementData;

// Internal data structure for SQLite
struct SQLiteData {
    sqlite3* handle = nullptr;
    std::filesystem::path path;
    bool isOpen = false;
    // Statements prepared on this connection, in creation order. Weak: a statement keeps its
    // database alive, not the other way round; close() finalizes whichever are still live.
    std::vector<std::weak_ptr<DBStatementData>> statements;

    // Idempotent cleanup - closes all statements and database
    void Close() noexcept;
    ~SQLiteData() noexcept { Close(); }
};

// SQLite class - provides SQLite database functionality
// Properties: path, statements, isOpen, lastRowId, changes
// Methods: execute, query, open, close
//
// Constructor signatures:
//   SQLite() - creates in-memory database (:memory:)
//   SQLite(path) - creates/opens database at path
//   SQLite(path, autoOpen) - creates database, optionally auto-open
class JSSQLite : public ClassBase<JSSQLite, SQLiteData> {
   public:
    static constexpr std::string_view ClassName = "SQLite";

    static std::unique_ptr<SQLiteData> New(const ub::CallbackInfo& args);

    static void Configure(const ub::Class<SQLiteData>& cls);
};

}  // namespace d2bs::api::classes
