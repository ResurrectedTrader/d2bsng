#pragma once

#include <sqlite3.h>
#include <v8.h>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"

namespace d2bs::api::classes {

// Forward declaration
struct DBStatementData;

// Internal data structure for SQLite
struct SQLiteData {
    sqlite3* handle = nullptr;
    std::filesystem::path path;
    bool isOpen = false;
    std::set<DBStatementData*> statements;

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
class JSSQLite : public V8ClassBase<JSSQLite, SQLiteData> {
   public:
    static constexpr std::string_view ClassName = "SQLite";

    // Constructor - creates a SQLite database connection
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::api::classes
