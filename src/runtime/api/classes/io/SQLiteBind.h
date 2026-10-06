#pragma once

#include <sqlite3.h>
#include <v8.h>
#include <string>

#include "api/core/Convert.h"

namespace d2bs::runtime::api::classes {

// Bind a single V8 value to a prepared sqlite3 statement at the given 1-based
// parameter index. Rules match the reference d2bs JS SQLite API:
//   null/undefined -> NULL
//   string         -> TEXT (copied via SQLITE_TRANSIENT)
//   number (int32) -> INTEGER
//   number (other) -> REAL
//   boolean        -> TEXT "true"/"false" (legacy contract)
//   ArrayBuffer, typed array / DataView -> BLOB of its bytes (copied)
//   other          -> returns false (caller throws appropriate error)
inline bool BindValue(v8::Isolate* isolate, v8::Local<v8::Value> value, sqlite3_stmt* handle, int32_t paramIdx) {
    if (value->IsArrayBuffer() || value->IsArrayBufferView()) {
        const uint8_t* bytes = nullptr;
        size_t size = 0;
        if (value->IsArrayBuffer()) {
            const auto buffer = value.As<v8::ArrayBuffer>();
            bytes = static_cast<const uint8_t*>(buffer->Data());
            size = buffer->ByteLength();
        } else {
            const auto view = value.As<v8::ArrayBufferView>();
            bytes = static_cast<const uint8_t*>(view->Buffer()->Data()) + view->ByteOffset();
            size = view->ByteLength();
        }
        // A null pointer would bind NULL, which an empty buffer is not.
        if (size == 0) {
            sqlite3_bind_zeroblob(handle, paramIdx, 0);
        } else {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
            sqlite3_bind_blob64(handle, paramIdx, bytes, size, SQLITE_TRANSIENT);
        }
        return true;
    }
    if (value->IsNullOrUndefined()) {
        sqlite3_bind_null(handle, paramIdx);
    } else if (value->IsString()) {
        std::string str = convert::ToString(isolate, value);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
        sqlite3_bind_text(handle, paramIdx, str.c_str(), static_cast<int32_t>(str.length()), SQLITE_TRANSIENT);
    } else if (value->IsNumber()) {
        if (value->IsInt32()) {
            sqlite3_bind_int(handle, paramIdx, convert::To<int32_t>(isolate, value));
        } else {
            sqlite3_bind_double(handle, paramIdx, convert::To<double>(isolate, value));
        }
    } else if (value->IsBoolean()) {
        const char* boolStr = value->BooleanValue(isolate) ? "true" : "false";
        sqlite3_bind_text(handle, paramIdx, boolStr, -1, SQLITE_STATIC);
    } else {
        return false;
    }
    return true;
}

}  // namespace d2bs::runtime::api::classes
