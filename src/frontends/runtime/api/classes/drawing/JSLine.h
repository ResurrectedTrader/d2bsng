#pragma once

#include <memory>

#include "JSDrawableBase.h"

namespace d2bs::api::classes {

class JSLine : public JSDrawableBase<JSLine, LineDrawable> {
   public:
    static constexpr std::string_view ClassName = "Line";

    /// @description Creates a line overlay drawable between two screen points.
    /// @signature Line(x?: number, y?: number, x2?: number, y2?: number, color?: number, automap?: boolean, click?:
    /// function, hover?: function)
    /// @param x {number} - First endpoint x coordinate.
    /// @param y {number} - First endpoint y coordinate.
    /// @param x2 {number} - Second endpoint x coordinate.
    /// @param y2 {number} - Second endpoint y coordinate.
    /// @param color {number} - Line color as a game color index.
    /// @param automap {boolean} - Whether the line is drawn on the automap.
    /// @param click {function} - Click handler.
    /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
    /// game (block votes are not awaited in the current build) (note: Line is not hit-tested, so this never fires)
    /// @param hover {function} - Hover handler.
    /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
    /// (entered = false, x and y are 0 on leave); return value ignored (note: Line is not hit-tested, so this never
    /// fires)
    /// @returns {Line} - The new Line drawable.
    /// @throws {Error} - when called outside a running script (no owning script context).
    static std::shared_ptr<LineDrawable> New(const ub::CallbackInfo& args) {
        const auto& context = args.GetContext();

        auto* script = ScriptEngine::Instance().GetScript(&args.GetIsolate());
        if (!script) {
            error::ThrowError(args.GetIsolate(), "Line: no owning script");
            return nullptr;
        }

        auto drawable = std::make_shared<LineDrawable>();

        extract::PointInto(args, 0, drawable->pos);
        extract::PointInto(args, 2, drawable->p2);

        if (args[4].IsNumber()) {
            drawable->color.store(convert::ToUint32(context, args[4]));
        }
        if (args[5].IsBoolean()) {
            drawable->isAutomap.store(args[5].IsTrue());
        }
        SetConstructorHandlers(args, *script, *drawable, 6);

        script->AddDrawable(drawable);
        return drawable;
    }

    static void Configure(const ub::Class<LineDrawable>& cls) {
        ConfigureCommonProperties(cls);

        /// @description X coordinate of the line's second endpoint
        /// @type {number}
        Property(
            cls, "x2",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->p2.load().x);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                auto cur = drawable->p2.load();
                cur.x = convert::ToInt32(info.GetContext(), value);
                drawable->p2.store(cur);
            });

        /// @description Y coordinate of the line's second endpoint
        /// @type {number}
        Property(
            cls, "y2",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->p2.load().y);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                auto cur = drawable->p2.load();
                cur.y = convert::ToInt32(info.GetContext(), value);
                drawable->p2.store(cur);
            });

        /// @description Line draw color as a game color palette index
        /// @type {number}
        Property(
            cls, "color",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->color.load());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                drawable->color.store(convert::ToUint32(info.GetContext(), value));
            });
    }
};

}  // namespace d2bs::api::classes
