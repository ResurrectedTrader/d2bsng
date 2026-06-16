#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <v8.h>

#include "game/Types.h"

// Type conversion utilities for V8 <-> C++
// All string operations use UTF-8

namespace d2bs::api::v8_convert {

// ============================================================================
// To V8 conversions
// ============================================================================

inline v8::Local<v8::String> ToV8(v8::Isolate* isolate, const char* str) {
    if (!str)
        return v8::String::Empty(isolate);
    v8::Local<v8::String> result;
    if (v8::String::NewFromUtf8(isolate, str).ToLocal(&result))
        return result;
    return v8::String::Empty(isolate);
}

// Keep both std::string and std::string_view overloads to prevent ambiguity
// with the filesystem::path overload (std::string implicitly converts to path).
inline v8::Local<v8::String> ToV8(v8::Isolate* isolate, const std::string& str) {
    v8::Local<v8::String> result;
    if (v8::String::NewFromUtf8(isolate, str.c_str(), v8::NewStringType::kNormal, static_cast<int32_t>(str.length()))
            .ToLocal(&result))
        return result;
    return v8::String::Empty(isolate);
}

inline v8::Local<v8::String> ToV8(v8::Isolate* isolate, std::string_view str) {
    v8::Local<v8::String> result;
    if (v8::String::NewFromUtf8(isolate, str.data(), v8::NewStringType::kNormal, static_cast<int32_t>(str.length()))
            .ToLocal(&result))
        return result;
    return v8::String::Empty(isolate);
}

// Convert filesystem path to UTF-8 V8 string (handles Unicode paths on Windows)
inline v8::Local<v8::String> ToV8(v8::Isolate* isolate, const std::filesystem::path& path) {
    auto u8Path = path.u8string();
    v8::Local<v8::String> result;
    if (v8::String::NewFromUtf8(isolate, reinterpret_cast<const char*>(u8Path.data()), v8::NewStringType::kNormal,
                                static_cast<int32_t>(u8Path.size()))
            .ToLocal(&result))
        return result;
    return v8::String::Empty(isolate);
}

inline v8::Local<v8::Integer> ToV8(v8::Isolate* isolate, int32_t val) {
    return v8::Integer::New(isolate, val);
}

inline v8::Local<v8::Integer> ToV8(v8::Isolate* isolate, uint32_t val) {
    return v8::Integer::NewFromUnsigned(isolate, val);
}

inline v8::Local<v8::Number> ToV8(v8::Isolate* isolate, double val) {
    return v8::Number::New(isolate, val);
}

inline v8::Local<v8::Boolean> ToV8(v8::Isolate* isolate, bool val) {
    return v8::Boolean::New(isolate, val);
}

// {x: uint32, y: uint32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, d2bs::game::Position pos) {
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->Set(context, ToV8(isolate, "x"), ToV8(isolate, pos.x)).Check();
    obj->Set(context, ToV8(isolate, "y"), ToV8(isolate, pos.y)).Check();
    return obj;
}

// {x: int32, y: int32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, d2bs::game::Point pt) {
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->Set(context, ToV8(isolate, "x"), ToV8(isolate, pt.x)).Check();
    obj->Set(context, ToV8(isolate, "y"), ToV8(isolate, pt.y)).Check();
    return obj;
}

// {width: uint32, height: uint32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, d2bs::game::Size sz) {
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->Set(context, ToV8(isolate, "width"), ToV8(isolate, sz.width)).Check();
    obj->Set(context, ToV8(isolate, "height"), ToV8(isolate, sz.height)).Check();
    return obj;
}

// ============================================================================
// From V8 conversions
// ============================================================================

inline std::string ToString(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || val->IsNullOrUndefined()) {
        return "";
    }
    v8::String::Utf8Value utf8(isolate, val);
    return *utf8 ? std::string(*utf8, utf8.length()) : "";
}

inline int32_t ToInt32(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || val->IsNullOrUndefined()) {
        return 0;
    }
    return val->Int32Value(isolate->GetCurrentContext()).FromMaybe(0);
}

inline uint32_t ToUint32(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || val->IsNullOrUndefined()) {
        return 0;
    }
    return val->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
}

inline double ToDouble(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || val->IsNullOrUndefined()) {
        return 0.0;
    }
    return val->NumberValue(isolate->GetCurrentContext()).FromMaybe(0.0);
}

inline bool ToBool(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty()) {
        return false;
    }
    return val->BooleanValue(isolate);
}

}  // namespace d2bs::api::v8_convert
