#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "game/Types.h"
#include "unibind/unibind.h"

// Native values into script values and back. The primitives take the isolate; anything that builds
// an object takes the context it is built in.

namespace d2bs::api::convert {

// A string, or an empty handle if one could not be made (the engine is out of memory). Lossy: bytes
// that are not UTF-8 each become U+FFFD rather than failing, since callers hand over game strings,
// file contents and socket reads as readily as their own literals.
inline ub::Local<ub::String> ToJS(ub::Isolate& isolate, std::string_view str) {
    return ub::String::NewFromUtf8(isolate, str).value_or(ub::Local<ub::String>());
}

inline ub::Local<ub::String> ToJS(ub::Isolate& isolate, const char* str) {
    return ToJS(isolate, str == nullptr ? std::string_view() : std::string_view(str));
}

inline ub::Local<ub::String> ToJS(ub::Isolate& isolate, const std::string& str) {
    return ToJS(isolate, std::string_view(str));
}

inline ub::Local<ub::String> ToJS(ub::Isolate& isolate, const std::filesystem::path& path) {
    const auto u8Path = path.u8string();
    return ToJS(isolate, std::string_view(reinterpret_cast<const char*>(u8Path.data()), u8Path.size()));
}

inline ub::Local<ub::Integer> ToJS(ub::Isolate& isolate, int32_t val) {
    return ub::Integer::New(isolate, val);
}

inline ub::Local<ub::Integer> ToJS(ub::Isolate& isolate, uint32_t val) {
    return ub::Integer::NewFromUnsigned(isolate, val);
}

inline ub::Local<ub::Number> ToJS(ub::Isolate& isolate, double val) {
    return ub::Number::New(isolate, val);
}

inline ub::Local<ub::Boolean> ToJS(ub::Isolate& isolate, bool val) {
    return ub::Boolean::New(isolate, val);
}

namespace detail {

// `{name: value, ...}` from a list of pairs. Empty if any step failed, which leaves the engine's
// exception (if it raised one) pending for the caller to return through.
template <class... Pairs>
std::optional<ub::Local<ub::Object>> MakeObject(const ub::Context& context, const Pairs&... pairs) {
    auto obj = ub::Object::New(context);
    if (!obj) {
        return std::nullopt;
    }
    const bool ok = (obj->Set(context, pairs.first, pairs.second).value_or(false) && ...);
    if (!ok) {
        return std::nullopt;
    }
    return obj;
}

}  // namespace detail

inline std::optional<ub::Local<ub::Object>> ToJS(const ub::Context& context, game::Position pos) {
    auto& isolate = context.GetIsolate();
    return detail::MakeObject(context, std::pair{"x", ToJS(isolate, pos.x)}, std::pair{"y", ToJS(isolate, pos.y)});
}

inline std::optional<ub::Local<ub::Object>> ToJS(const ub::Context& context, game::Point pt) {
    auto& isolate = context.GetIsolate();
    return detail::MakeObject(context, std::pair{"x", ToJS(isolate, pt.x)}, std::pair{"y", ToJS(isolate, pt.y)});
}

inline std::optional<ub::Local<ub::Object>> ToJS(const ub::Context& context, game::Size sz) {
    auto& isolate = context.GetIsolate();
    return detail::MakeObject(context, std::pair{"width", ToJS(isolate, sz.width)},
                              std::pair{"height", ToJS(isolate, sz.height)});
}

inline std::optional<ub::Local<ub::Object>> ToJS(const ub::Context& context, const game::StatEntry& stat) {
    auto& isolate = context.GetIsolate();
    return detail::MakeObject(context, std::pair{"id", ToJS(isolate, stat.statId)},
                              std::pair{"layer", ToJS(isolate, stat.subIndex)},
                              std::pair{"value", ToJS(isolate, stat.value)});
}

inline std::optional<ub::Local<ub::Object>> ToJS(const ub::Context& context, const game::StatListEntry& list) {
    auto& isolate = context.GetIsolate();
    auto stats = ub::Array::New(context, static_cast<uint32_t>(list.stats.size()));
    if (!stats) {
        return std::nullopt;
    }
    for (uint32_t i = 0; i < list.stats.size(); ++i) {
        auto entry = ToJS(context, list.stats[i]);
        if (!entry || !stats->Set(context, i, *entry).value_or(false)) {
            return std::nullopt;
        }
    }
    return detail::MakeObject(context, std::pair{"flags", ToJS(isolate, list.flags)},
                              std::pair{"stateNo", ToJS(isolate, list.stateNo)},
                              std::pair{"stats", ub::Local<ub::Value>(*stats)});
}

// Script values into native ones, with the reference's leniency: an absent, null or undefined value,
// or one whose conversion throws, is the type's zero value rather than an error. A conversion that
// threw leaves its exception pending, as it would in the engine's own coercion.
inline std::string ToString(const ub::Context& context, const ub::Local<ub::Value>& val) {
    if (val.IsEmpty() || val.IsNullOrUndefined()) {
        return "";
    }
    auto text = val.ToString(context);
    return text ? text->Utf8Value() : std::string();
}

inline int32_t ToInt32(const ub::Context& context, const ub::Local<ub::Value>& val) {
    if (val.IsEmpty() || val.IsNullOrUndefined()) {
        return 0;
    }
    return val.ToInt32(context).value_or(0);
}

inline uint32_t ToUint32(const ub::Context& context, const ub::Local<ub::Value>& val) {
    if (val.IsEmpty() || val.IsNullOrUndefined()) {
        return 0;
    }
    return val.ToUint32(context).value_or(0);
}

inline double ToDouble(const ub::Context& context, const ub::Local<ub::Value>& val) {
    if (val.IsEmpty() || val.IsNullOrUndefined()) {
        return 0.0;
    }
    return val.ToNumber(context).value_or(0.0);
}

// Truthiness, which cannot throw - an empty handle is false.
inline bool ToBool(const ub::Context& context, const ub::Local<ub::Value>& val) {
    return !val.IsEmpty() && val.ToBoolean(context).value_or(false);
}

}  // namespace d2bs::api::convert
