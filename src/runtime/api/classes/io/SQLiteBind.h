#pragma once

#include <sqlite3.h>
#include <v8.h>
#include <format>
#include <string>

#include "api/core/Convert.h"
#include "api/core/Error.h"
// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "RuntimeEnumNames.h"

namespace d2bs::runtime::api::classes {

enum class BindResult : uint8_t {
    Bound,
    // Not a bindable value; nothing was reported, so the caller throws its own message.
    BadType,
    // SQLite refused the binding (a parameter index out of range, a statement that is mid-step); an Error is
    // pending.
    Failed,
};

// Bind a single V8 value to a prepared sqlite3 statement at the given 1-based
// parameter index. Rules match the reference d2bs JS SQLite API:
//   null/undefined -> NULL
//   string         -> TEXT (copied via SQLITE_TRANSIENT)
//   number (int32) -> INTEGER
//   number (other) -> REAL
//   boolean        -> INTEGER 1/0
//   ArrayBuffer, typed array / DataView -> BLOB of its bytes (copied)
//   other          -> BadType
inline BindResult BindValue(v8::Isolate* isolate, v8::Local<v8::Value> value, sqlite3_stmt* handle, int32_t paramIdx) {
    int32_t rc = SQLITE_OK;
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
            rc = sqlite3_bind_zeroblob(handle, paramIdx, 0);
        } else {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
            rc = sqlite3_bind_blob64(handle, paramIdx, bytes, size, SQLITE_TRANSIENT);
        }
    } else if (value->IsNullOrUndefined()) {
        rc = sqlite3_bind_null(handle, paramIdx);
    } else if (value->IsBoolean()) {
        rc = sqlite3_bind_int(handle, paramIdx, value->BooleanValue(isolate) ? 1 : 0);
    } else if (value->IsString()) {
        std::string str = convert::ToString(isolate, value);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast) - C-style cast in sqlite3 macro
        rc = sqlite3_bind_text(handle, paramIdx, str.c_str(), static_cast<int32_t>(str.length()), SQLITE_TRANSIENT);
    } else if (value->IsNumber()) {
        if (value->IsInt32()) {
            rc = sqlite3_bind_int(handle, paramIdx, convert::To<int32_t>(isolate, value));
        } else {
            rc = sqlite3_bind_double(handle, paramIdx, convert::To<double>(isolate, value));
        }
    } else {
        return BindResult::BadType;
    }
    if (rc != SQLITE_OK) {
        error::ThrowError(isolate, std::format("Could not bind parameter {}: {}", paramIdx, sqlite3_errstr(rc)));
        return BindResult::Failed;
    }
    return BindResult::Bound;
}

}  // namespace d2bs::runtime::api::classes
