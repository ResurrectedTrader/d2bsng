#pragma once

#include <sqlite3.h>
#include <string>

#include "api/core/Convert.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Bind a single script value to a prepared sqlite3 statement at the given 1-based
// parameter index. Rules match the reference d2bs JS SQLite API:
//   null/undefined -> NULL
//   string         -> TEXT (copied via SQLITE_TRANSIENT)
//   number (int32) -> INTEGER
//   number (other) -> REAL
//   boolean        -> TEXT "true"/"false" (legacy contract)
//   other          -> returns false (caller throws appropriate error)
inline bool BindValue(const ub::Context& context, const ub::Local<ub::Value>& value, sqlite3_stmt* handle,
                      int32_t paramIdx) {
    if (value.IsNullOrUndefined()) {
        sqlite3_bind_null(handle, paramIdx);
    } else if (value.IsString()) {
        std::string str = convert::ToString(context, value);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
        sqlite3_bind_text(handle, paramIdx, str.c_str(), static_cast<int32_t>(str.length()), SQLITE_TRANSIENT);
    } else if (value.IsNumber()) {
        if (value.IsInt32()) {
            sqlite3_bind_int(handle, paramIdx, convert::ToInt32(context, value));
        } else {
            sqlite3_bind_double(handle, paramIdx, convert::ToDouble(context, value));
        }
    } else if (value.IsBoolean()) {
        const char* boolStr = value.IsTrue() ? "true" : "false";
        sqlite3_bind_text(handle, paramIdx, boolStr, -1, SQLITE_STATIC);
    } else {
        return false;
    }
    return true;
}

}  // namespace d2bs::api::classes
