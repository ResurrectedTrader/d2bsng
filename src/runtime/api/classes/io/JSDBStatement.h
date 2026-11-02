#pragma once

#include <sqlite3.h>
#include <v8.h>
#include <string>
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"

namespace d2bs::runtime::api::classes {

// Forward declaration
struct SQLiteData;

// Internal data structure for DBStatement
// Defined in header so SQLite.cpp can allocate instances. Owns handle from construction and registers with
// parent, which finalizes the statement if it is closed first.
struct DBStatementData {
    sqlite3_stmt* handle = nullptr;             // prepared statement handle
    SQLiteData* parent = nullptr;               // parent database (for error messages)
    std::string sql;                            // the SQL string (for debugging)
    bool isOpen = false;                        // whether statement is open
    bool hasRow = false;                        // whether a row is ready to read
    v8::TracedReference<v8::Object> cachedRow;  // cached current row object, read only while hasRow
    // The wrapper that owns this native. SQLite.statements views keep it alive through a private.
    v8::TracedReference<v8::Object> owner;
    // The parent's SQLite wrapper, so the database outlives every reachable statement and `parent` stays valid.
    v8::TracedReference<v8::Object> parentObject;

    DBStatementData(sqlite3_stmt* stmt, SQLiteData* db, std::string text);
    DBStatementData(const DBStatementData&) = delete;
    DBStatementData& operator=(const DBStatementData&) = delete;
    DBStatementData(DBStatementData&&) = delete;
    DBStatementData& operator=(DBStatementData&&) = delete;

    // Idempotent cleanup - finalizes statement and removes from parent. Runs from the destructor and PreFinalize too,
    // so it touches no V8 value; the GC reclaims cachedRow.
    void Finalize();
    // Finalize from a binding: also lets go of the cached row and the database, which a finalized statement no
    // longer reads.
    void Close();
    ~DBStatementData() { Finalize(); }
    // Leaves the parent's list as soon as the GC finds the wrapper dead, so SQLite.statements never views a
    // statement whose wrapper is gone but not yet swept.
    void PreFinalize() { Finalize(); }

    void Trace(cppgc::Visitor* visitor) const {
        visitor->Trace(cachedRow);
        visitor->Trace(owner);
        visitor->Trace(parentObject);
    }
};

// DBStatement class - represents a prepared SQLite statement
// Properties: sql, ready
// Methods: getObject, getColumnCount, getColumnName, getColumnValue,
//          go, next, skip, reset, close, bind

class JSDBStatement : public ClassBase<JSDBStatement, DBStatementData> {
   public:
    static constexpr std::string_view ClassName = "DBStatement";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::runtime::api::classes
