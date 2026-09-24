#pragma once

#include <memory>

#include "JSDrawableBase.h"

namespace d2bs::api::classes {

class JSBox : public JSDrawableBase<JSBox, BoxDrawable> {
   public:
    static constexpr std::string_view ClassName = "Box";

    /// @description Creates a filled-rectangle overlay drawable at a screen position.
    /// @signature Box(x?: number, y?: number, xsize?: number, ysize?: number, color?: number, opacity?: number, align?:
    /// number, automap?: boolean, click?: function, hover?: function)
    /// @param x {number} - Screen x coordinate.
    /// @param y {number} - Screen y coordinate.
    /// @param xsize {number} - Box width in pixels.
    /// @param ysize {number} - Box height in pixels.
    /// @param color {number} - Fill color as a game color index.
    /// @param opacity {number} - Fill opacity.
    /// @param align {number} - Horizontal alignment (0 = Left, 1 = Right, 2 = Center).
    /// @param automap {boolean} - Whether the box is drawn on the automap.
    /// @param click {function} - Click handler.
    /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
    /// game (block votes are not awaited in the current build)
    /// @param hover {function} - Hover handler.
    /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
    /// (entered = false, x and y are 0 on leave); return value ignored
    /// @returns {Box} - The new Box drawable.
    /// @throws {Error} - when called outside a running script (no owning script context).
    static std::shared_ptr<BoxDrawable> New(const ub::CallbackInfo& args) {
        const auto& context = args.GetContext();

        auto* script = ScriptEngine::Instance().GetScript(&args.GetIsolate());
        if (!script) {
            error::ThrowError(args.GetIsolate(), "Box: no owning script");
            return nullptr;
        }

        auto drawable = std::make_shared<BoxDrawable>();

        extract::PointInto(args, 0, drawable->pos);
        extract::SizeInto(args, 2, drawable->size);

        if (args[4].IsNumber()) {
            drawable->color.store(convert::ToUint32(context, args[4]));
        }
        if (args[5].IsNumber()) {
            drawable->opacity.store(convert::ToUint32(context, args[5]));
        }
        if (args[6].IsNumber()) {
            drawable->align.store(static_cast<Align>(convert::ToInt32(context, args[6])));
        }
        if (args[7].IsBoolean()) {
            drawable->isAutomap.store(args[7].IsTrue());
        }
        SetConstructorHandlers(args, *script, *drawable, 8);

        script->AddDrawable(drawable);
        return drawable;
    }

    static void Configure(const ub::Class<BoxDrawable>& cls) {
        ConfigureCommonProperties(cls);

        /// @description Box width in pixels (alias for the width property).
        /// @type {number}
        Property(
            cls, "xsize",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->size.load().width);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                auto cur = drawable->size.load();
                // Note: Size is unsigned; negative values coerce to large positive via ToUint32.
                cur.width = convert::ToUint32(info.GetContext(), value);
                drawable->size.store(cur);
            });

        /// @description Box height in pixels (alias for the height property).
        /// @type {number}
        Property(
            cls, "ysize",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->size.load().height);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                auto cur = drawable->size.load();
                // Note: Size is unsigned; negative values coerce to large positive via ToUint32.
                cur.height = convert::ToUint32(info.GetContext(), value);
                drawable->size.store(cur);
            });

        /// @description Box fill color as a game color index.
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

        /// @description Box fill opacity.
        /// @type {number}
        Property(
            cls, "opacity",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->opacity.load());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                drawable->opacity.store(convert::ToUint32(info.GetContext(), value));
            });
    }
};

}  // namespace d2bs::api::classes
