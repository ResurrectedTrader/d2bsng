#pragma once

#include <filesystem>
#include <string>
#include <vector>

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

namespace detail {

// Property names for the fixed-shape objects below. Interned so the engine doesn't
// re-hash them into the string table on every coordinate handed to JS, and Eternal so
// the handles survive without a HandleScope. getPath alone builds one object per path
// point, so these were the hottest string allocations in the binding layer.
//
// Only the names are cached. Nothing here roots a context: an Eternal is never
// releasable, so caching anything reachable from the realm (Object.prototype, say)
// would pin the script's globals past TeardownIsolate's root-clearing and stop the
// pre-Dispose GC from firing the weak callbacks that free native wrapper structs.
struct KeyCache {
    v8::Isolate* isolate = nullptr;
    v8::Eternal<v8::Name> x;
    v8::Eternal<v8::Name> y;
    v8::Eternal<v8::Name> width;
    v8::Eternal<v8::Name> height;
    v8::Eternal<v8::Name> id;
    v8::Eternal<v8::Name> layer;
    v8::Eternal<v8::Name> value;
    v8::Eternal<v8::Name> flags;
    v8::Eternal<v8::Name> stateNo;
    v8::Eternal<v8::Name> stats;
};

inline thread_local KeyCache keyCache;

// Requires an entered HandleScope - every caller is inside a V8 callback.
inline KeyCache& Keys(v8::Isolate* isolate) {
    if (keyCache.isolate == isolate) {
        return keyCache;
    }
    // Degrades to an empty name like the ToV8 overloads above rather than going fatal. The only
    // way this fails is heap exhaustion, and ToLocalChecked would take the whole process down
    // through the isolate's fatal handler - killing every other script in it.
    bool interned = true;
    auto intern = [isolate, &interned](const char* name) {
        v8::Local<v8::String> result;
        if (v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocal(&result)) {
            return result;
        }
        interned = false;
        return v8::String::Empty(isolate);
    };
    keyCache.x.Set(isolate, intern("x"));
    keyCache.y.Set(isolate, intern("y"));
    keyCache.width.Set(isolate, intern("width"));
    keyCache.height.Set(isolate, intern("height"));
    keyCache.id.Set(isolate, intern("id"));
    keyCache.layer.Set(isolate, intern("layer"));
    keyCache.value.Set(isolate, intern("value"));
    keyCache.flags.Set(isolate, intern("flags"));
    keyCache.stateNo.Set(isolate, intern("stateNo"));
    keyCache.stats.Set(isolate, intern("stats"));
    // Published only once every name is real, and last, so a partial run leaves the cache
    // invalid rather than valid with an empty slot - the next call retries.
    if (interned) {
        keyCache.isolate = isolate;
    }
    return keyCache;
}

}  // namespace detail

// Drop this thread's cached keys. Only this thread's copy needs clearing: an isolate is
// created and torn down on its own thread, so no other thread holds a cache for it. Eternal
// has no release, so invalidating other threads would orphan their slots rather than free
// them.
inline void ClearKeyCache(v8::Isolate* isolate) {
    if (detail::keyCache.isolate == isolate) {
        detail::keyCache.isolate = nullptr;
    }
}

// {x: uint32, y: uint32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, game::Position pos) {
    auto& keys = detail::Keys(isolate);
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->CreateDataProperty(context, keys.x.Get(isolate), ToV8(isolate, pos.x)).Check();
    obj->CreateDataProperty(context, keys.y.Get(isolate), ToV8(isolate, pos.y)).Check();
    return obj;
}

// {x: int32, y: int32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, game::Point pt) {
    auto& keys = detail::Keys(isolate);
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->CreateDataProperty(context, keys.x.Get(isolate), ToV8(isolate, pt.x)).Check();
    obj->CreateDataProperty(context, keys.y.Get(isolate), ToV8(isolate, pt.y)).Check();
    return obj;
}

// {width: uint32, height: uint32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, game::Size sz) {
    auto& keys = detail::Keys(isolate);
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->CreateDataProperty(context, keys.width.Get(isolate), ToV8(isolate, sz.width)).Check();
    obj->CreateDataProperty(context, keys.height.Get(isolate), ToV8(isolate, sz.height)).Check();
    return obj;
}

// {id: uint32, layer: uint32, value: int32}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, const game::StatEntry& stat) {
    auto& keys = detail::Keys(isolate);
    auto context = isolate->GetCurrentContext();
    auto obj = v8::Object::New(isolate);
    obj->CreateDataProperty(context, keys.id.Get(isolate), ToV8(isolate, stat.statId)).Check();
    obj->CreateDataProperty(context, keys.layer.Get(isolate), ToV8(isolate, stat.subIndex)).Check();
    obj->CreateDataProperty(context, keys.value.Get(isolate), ToV8(isolate, stat.value)).Check();
    return obj;
}

// {flags: uint32, stateNo: uint32, stats: StatEntry[]}
inline v8::Local<v8::Object> ToV8(v8::Isolate* isolate, const game::StatListEntry& list) {
    auto& keys = detail::Keys(isolate);
    auto context = isolate->GetCurrentContext();
    // One allocation, packed elements - the length-then-Set form starts holey and takes
    // the generic store path per element.
    std::vector<v8::Local<v8::Value>> elements;
    elements.reserve(list.stats.size());
    for (const auto& stat : list.stats) {
        elements.emplace_back(ToV8(isolate, stat));
    }
    auto stats = v8::Array::New(isolate, elements.data(), elements.size());
    auto obj = v8::Object::New(isolate);
    obj->CreateDataProperty(context, keys.flags.Get(isolate), ToV8(isolate, list.flags)).Check();
    obj->CreateDataProperty(context, keys.stateNo.Get(isolate), ToV8(isolate, list.stateNo)).Check();
    obj->CreateDataProperty(context, keys.stats.Get(isolate), stats).Check();
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
