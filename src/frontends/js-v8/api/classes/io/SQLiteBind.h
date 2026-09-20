#pragma once

#include <sqlite3.h>
#include <v8.h>
#include <string>

#include "api/core/V8Convert.h"

namespace d2bs::api::classes {

// Bind a single V8 value to a prepared sqlite3 statement at the given 1-based
// parameter index. Rules match the reference d2bs JS SQLite API:
//   null/undefined -> NULL
//   string         -> TEXT (copied via SQLITE_TRANSIENT)
//   number (int32) -> INTEGER
//   number (other) -> REAL
//   boolean        -> TEXT "true"/"false" (legacy contract)
//   other          -> returns false (caller throws appropriate error)
inline bool BindValue(v8::Isolate* isolate, v8::Local<v8::Value> value, sqlite3_stmt* handle, int32_t paramIdx) {
    if (value->IsNullOrUndefined()) {
        sqlite3_bind_null(handle, paramIdx);
    } else if (value->IsString()) {
        std::string str = v8_convert::ToString(isolate, value);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
        sqlite3_bind_text(handle, paramIdx, str.c_str(), static_cast<int32_t>(str.length()), SQLITE_TRANSIENT);
    } else if (value->IsNumber()) {
        if (value->IsInt32()) {
            sqlite3_bind_int(handle, paramIdx, v8_convert::ToInt32(isolate, value));
        } else {
            sqlite3_bind_double(handle, paramIdx, v8_convert::ToDouble(isolate, value));
        }
    } else if (value->IsBoolean()) {
        const char* boolStr = value->BooleanValue(isolate) ? "true" : "false";
        sqlite3_bind_text(handle, paramIdx, boolStr, -1, SQLITE_STATIC);
    } else {
        return false;
    }
    return true;
}

}  // namespace d2bs::api::classes
