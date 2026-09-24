#pragma once

#include <sqlite3.h>
#include <memory>
#include <string>

#include "api/core/Class.h"

namespace d2bs::api::classes {

struct SQLiteData;

// Internal data structure for DBStatement
// Defined in header so SQLite.cpp can allocate instances
struct DBStatementData {
    sqlite3_stmt* handle = nullptr;      // prepared statement handle
    std::shared_ptr<SQLiteData> parent;  // owning database, kept alive until finalize
    std::string sql;                     // the SQL string (for debugging)
    bool isOpen = false;                 // whether statement is open
    bool hasRow = false;                 // whether a row is ready to read
    ub::Global<ub::Object> cachedRow;    // cached current row object

    // Idempotent cleanup - finalizes statement and removes from parent
    void Finalize();
    ~DBStatementData() { Finalize(); }
};

// DBStatement class - represents a prepared SQLite statement
// Properties: sql, ready
// Methods: getObject, getColumnCount, getColumnName, getColumnValue,
//          go, next, skip, reset, close, bind
class JSDBStatement : public ClassBase<JSDBStatement, DBStatementData> {
   public:
    static constexpr std::string_view ClassName = "DBStatement";

    static void Configure(const ub::Class<DBStatementData>& cls);
};

}  // namespace d2bs::api::classes
