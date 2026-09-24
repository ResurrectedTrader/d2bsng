#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "api/core/Convert.h"
#include "game/Types.h"
#include "unibind/unibind.h"

// Game geometry out of script values: an `{x, y}` / `{width, height}` object, or a pair of
// adjacent arguments.

namespace d2bs::api::extract {

namespace detail {

// Two numeric properties of an object, or nothing if it is not an object, a read threw, or either
// is not a number.
inline std::optional<std::pair<ub::Local<ub::Value>, ub::Local<ub::Value>>> Pair(const ub::Context& context,
                                                                                 const ub::Local<ub::Value>& val,
                                                                                 std::string_view first,
                                                                                 std::string_view second) {
    if (val.IsEmpty() || !val.IsObject()) {
        return std::nullopt;
    }
    auto obj = val.To<ub::Object>();
    if (!obj) {
        return std::nullopt;
    }
    auto a = obj->Get(context, first);
    auto b = obj->Get(context, second);
    if (!a || !b || !a->IsNumber() || !b->IsNumber()) {
        return std::nullopt;
    }
    return std::pair{*a, *b};
}

}  // namespace detail

inline std::optional<game::Position> Position(const ub::Context& context, const ub::Local<ub::Value>& val) {
    auto xy = detail::Pair(context, val, "x", "y");
    if (!xy) {
        return std::nullopt;
    }
    return game::Position{.x = convert::ToUint32(context, xy->first), .y = convert::ToUint32(context, xy->second)};
}

inline std::optional<game::Point> Point(const ub::Context& context, const ub::Local<ub::Value>& val) {
    auto xy = detail::Pair(context, val, "x", "y");
    if (!xy) {
        return std::nullopt;
    }
    return game::Point{.x = convert::ToInt32(context, xy->first), .y = convert::ToInt32(context, xy->second)};
}

inline std::optional<game::Size> Size(const ub::Context& context, const ub::Local<ub::Value>& val) {
    auto wh = detail::Pair(context, val, "width", "height");
    if (!wh) {
        return std::nullopt;
    }
    return game::Size{.width = convert::ToUint32(context, wh->first), .height = convert::ToUint32(context, wh->second)};
}

// Two adjacent arguments starting at `idx`.
inline std::optional<game::Position> Position(const ub::CallbackInfo& args, uint32_t idx) {
    if (args.Length() <= idx + 1) {
        return std::nullopt;
    }
    const auto& context = args.GetContext();
    return game::Position{.x = convert::ToUint32(context, args[idx]), .y = convert::ToUint32(context, args[idx + 1])};
}

inline std::optional<game::Point> Point(const ub::CallbackInfo& args, uint32_t idx) {
    if (args.Length() <= idx + 1) {
        return std::nullopt;
    }
    const auto& context = args.GetContext();
    return game::Point{.x = convert::ToInt32(context, args[idx]), .y = convert::ToInt32(context, args[idx + 1])};
}

inline std::optional<game::Size> Size(const ub::CallbackInfo& args, uint32_t idx) {
    if (args.Length() <= idx + 1) {
        return std::nullopt;
    }
    const auto& context = args.GetContext();
    return game::Size{.width = convert::ToUint32(context, args[idx]),
                      .height = convert::ToUint32(context, args[idx + 1])};
}

// Overwrite whichever halves of `out` the arguments at `idx` / `idx + 1` supply as numbers.
inline void PointInto(const ub::CallbackInfo& args, uint32_t idx, std::atomic<game::Point>& out) {
    const auto& context = args.GetContext();
    auto cur = out.load();
    if (args.Length() > idx && args[idx].IsNumber()) {
        cur.x = convert::ToInt32(context, args[idx]);
    }
    if (args.Length() > idx + 1 && args[idx + 1].IsNumber()) {
        cur.y = convert::ToInt32(context, args[idx + 1]);
    }
    out.store(cur);
}

inline void SizeInto(const ub::CallbackInfo& args, uint32_t idx, std::atomic<game::Size>& out) {
    const auto& context = args.GetContext();
    auto cur = out.load();
    if (args.Length() > idx && args[idx].IsNumber()) {
        cur.width = convert::ToUint32(context, args[idx]);
    }
    if (args.Length() > idx + 1 && args[idx + 1].IsNumber()) {
        cur.height = convert::ToUint32(context, args[idx + 1]);
    }
    out.store(cur);
}

}  // namespace d2bs::api::extract
