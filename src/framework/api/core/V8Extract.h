#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include <v8.h>

#include "api/core/V8Convert.h"
#include "game/Types.h"

// Extraction utilities for taking C++ values out of V8 args / objects.
//
// Names drop the Extract prefix - the namespace carries the verb.
// Inside this namespace, function names shadow the type names, so uses of the
// types must be qualified (d2bs::game::Point etc.).

namespace d2bs::api::v8_extract {

// ============================================================================
// From a v8::Value (object input, e.g. {x, y} or {width, height})
// ============================================================================

inline std::optional<d2bs::game::Position> Position(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || !val->IsObject())
        return std::nullopt;
    auto context = isolate->GetCurrentContext();
    auto obj = val.As<v8::Object>();
    v8::Local<v8::Value> xv;
    v8::Local<v8::Value> yv;
    if (!obj->Get(context, v8_convert::ToV8(isolate, "x")).ToLocal(&xv))
        return std::nullopt;
    if (!obj->Get(context, v8_convert::ToV8(isolate, "y")).ToLocal(&yv))
        return std::nullopt;
    if (!xv->IsNumber() || !yv->IsNumber())
        return std::nullopt;
    return d2bs::game::Position{.x = v8_convert::ToUint32(isolate, xv), .y = v8_convert::ToUint32(isolate, yv)};
}

inline std::optional<d2bs::game::Point> Point(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || !val->IsObject())
        return std::nullopt;
    auto context = isolate->GetCurrentContext();
    auto obj = val.As<v8::Object>();
    v8::Local<v8::Value> xv;
    v8::Local<v8::Value> yv;
    if (!obj->Get(context, v8_convert::ToV8(isolate, "x")).ToLocal(&xv))
        return std::nullopt;
    if (!obj->Get(context, v8_convert::ToV8(isolate, "y")).ToLocal(&yv))
        return std::nullopt;
    if (!xv->IsNumber() || !yv->IsNumber())
        return std::nullopt;
    return d2bs::game::Point{.x = v8_convert::ToInt32(isolate, xv), .y = v8_convert::ToInt32(isolate, yv)};
}

inline std::optional<d2bs::game::Size> Size(v8::Isolate* isolate, v8::Local<v8::Value> val) {
    if (val.IsEmpty() || !val->IsObject())
        return std::nullopt;
    auto context = isolate->GetCurrentContext();
    auto obj = val.As<v8::Object>();
    v8::Local<v8::Value> wv;
    v8::Local<v8::Value> hv;
    if (!obj->Get(context, v8_convert::ToV8(isolate, "width")).ToLocal(&wv))
        return std::nullopt;
    if (!obj->Get(context, v8_convert::ToV8(isolate, "height")).ToLocal(&hv))
        return std::nullopt;
    if (!wv->IsNumber() || !hv->IsNumber())
        return std::nullopt;
    return d2bs::game::Size{.width = v8_convert::ToUint32(isolate, wv), .height = v8_convert::ToUint32(isolate, hv)};
}

// ============================================================================
// From positional args (both args[idx] and args[idx+1] must be numeric)
// ============================================================================

// Positional overloads rely on v8_convert::ToUint32/ToInt32 to coerce strings/booleans
// per V8 semantics - matching the pre-refactor behavior where scripts like
// `getPath(area, "10", "20", ...)` silently worked via implicit coercion.
// Only the arg-count bound is enforced here.
inline std::optional<d2bs::game::Position> Position(const v8::FunctionCallbackInfo<v8::Value>& args, int idx) {
    if (args.Length() <= idx + 1)
        return std::nullopt;
    auto* isolate = args.GetIsolate();
    return d2bs::game::Position{.x = v8_convert::ToUint32(isolate, args[idx]),
                                .y = v8_convert::ToUint32(isolate, args[idx + 1])};
}

inline std::optional<d2bs::game::Point> Point(const v8::FunctionCallbackInfo<v8::Value>& args, int idx) {
    if (args.Length() <= idx + 1)
        return std::nullopt;
    auto* isolate = args.GetIsolate();
    return d2bs::game::Point{.x = v8_convert::ToInt32(isolate, args[idx]),
                             .y = v8_convert::ToInt32(isolate, args[idx + 1])};
}

inline std::optional<d2bs::game::Size> Size(const v8::FunctionCallbackInfo<v8::Value>& args, int idx) {
    if (args.Length() <= idx + 1)
        return std::nullopt;
    auto* isolate = args.GetIsolate();
    return d2bs::game::Size{.width = v8_convert::ToUint32(isolate, args[idx]),
                            .height = v8_convert::ToUint32(isolate, args[idx + 1])};
}

// ============================================================================
// Partial-update helpers for Drawable atomics.
//
// Read the current atomic, overlay per-arg IsNumber() writes, store back.
// Preserves "new Box(5) leaves y at its default" semantics: each arg is only
// applied if the corresponding args[idx]/args[idx+1] IsNumber().
// ============================================================================

inline void PointInto(const v8::FunctionCallbackInfo<v8::Value>& args, int idx, std::atomic<d2bs::game::Point>& out) {
    auto* isolate = args.GetIsolate();
    auto cur = out.load();
    if (args.Length() > idx && args[idx]->IsNumber())
        cur.x = v8_convert::ToInt32(isolate, args[idx]);
    if (args.Length() > idx + 1 && args[idx + 1]->IsNumber())
        cur.y = v8_convert::ToInt32(isolate, args[idx + 1]);
    out.store(cur);
}

inline void SizeInto(const v8::FunctionCallbackInfo<v8::Value>& args, int idx, std::atomic<d2bs::game::Size>& out) {
    auto* isolate = args.GetIsolate();
    auto cur = out.load();
    if (args.Length() > idx && args[idx]->IsNumber())
        cur.width = v8_convert::ToUint32(isolate, args[idx]);
    if (args.Length() > idx + 1 && args[idx + 1]->IsNumber())
        cur.height = v8_convert::ToUint32(isolate, args[idx + 1]);
    out.store(cur);
}

}  // namespace d2bs::api::v8_extract
