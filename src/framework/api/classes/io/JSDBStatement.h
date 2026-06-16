#pragma once

#include <sqlite3.h>
#include <v8.h>
#include <cstdint>
#include <string>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"

namespace d2bs::api::classes {

// Forward declaration
struct SQLiteData;

// Internal data structure for DBStatement
// Defined in header so SQLite.cpp can allocate instances
struct DBStatementData {
    sqlite3_stmt* handle = nullptr;    // prepared statement handle
    SQLiteData* parent = nullptr;      // parent database (for error messages)
    std::string sql;                   // the SQL string (for debugging)
    bool isOpen = false;               // whether statement is open
    bool hasRow = false;               // whether a row is ready to read
    v8::Global<v8::Object> cachedRow;  // cached current row object

    // Idempotent cleanup - finalizes statement and removes from parent
    void Finalize();
    ~DBStatementData() { Finalize(); }
};

// DBStatement class - represents a prepared SQLite statement
// Properties: sql, ready
// Methods: getObject, getColumnCount, getColumnName, getColumnValue,
//          go, next, skip, reset, close, bind

class JSDBStatement : public V8ClassBase<JSDBStatement, DBStatementData> {
   public:
    static constexpr std::string_view ClassName = "DBStatement";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::api::classes
