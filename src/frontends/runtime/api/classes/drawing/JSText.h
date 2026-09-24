#pragma once

#include <memory>

#include "JSDrawableBase.h"

namespace d2bs::api::classes {

class JSText : public JSDrawableBase<JSText, TextDrawable> {
   public:
    static constexpr std::string_view ClassName = "Text";

    /// @description Creates a text overlay drawable at a screen position.
    /// @signature Text(text?: string, x?: number, y?: number, color?: number, font?: number, align?: number, automap?:
    /// boolean, click?: function, hover?: function)
    /// @param text {string} - The text to display.
    /// @param x {number} - Screen x coordinate.
    /// @param y {number} - Screen y coordinate.
    /// @param color {number} - Text color as a game color index.
    /// @param font {number} - Font index used to render the text.
    /// @param align {number} - Horizontal alignment (0 = Left, 1 = Right, 2 = Center).
    /// @param automap {boolean} - Whether the text is drawn on the automap.
    /// @param click {function} - Click handler.
    /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
    /// game (block votes are not awaited in the current build)
    /// @param hover {function} - Hover handler.
    /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
    /// (entered = false, x and y are 0 on leave); return value ignored
    /// @returns {Text} - The new Text drawable.
    /// @throws {Error} - when called outside a running script (no owning script context).
    static std::shared_ptr<TextDrawable> New(const ub::CallbackInfo& args) {
        const auto& context = args.GetContext();

        auto* script = ScriptEngine::Instance().GetScript(&args.GetIsolate());
        if (!script) {
            error::ThrowError(args.GetIsolate(), "Text: no owning script");
            return nullptr;
        }

        auto drawable = std::make_shared<TextDrawable>();

        if (args[0].IsString()) {
            drawable->SetText(convert::ToString(context, args[0]));
        }
        extract::PointInto(args, 1, drawable->pos);

        if (args[3].IsNumber()) {
            drawable->color.store(convert::ToUint32(context, args[3]));
        }
        if (args[4].IsNumber()) {
            drawable->font.store(convert::ToInt32(context, args[4]));
        }
        if (args[5].IsNumber()) {
            drawable->align.store(static_cast<Align>(convert::ToInt32(context, args[5])));
        }
        if (args[6].IsBoolean()) {
            drawable->isAutomap.store(args[6].IsTrue());
        }
        SetConstructorHandlers(args, *script, *drawable, 7);

        script->AddDrawable(drawable);
        return drawable;
    }

    static void Configure(const ub::Class<TextDrawable>& cls) {
        ConfigureCommonProperties(cls);

        /// @description The displayed text string of this Text overlay.
        /// @type {string}
        Property(
            cls, "text",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), drawable->GetText()));
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsString()) {
                    return;
                }
                drawable->SetText(convert::ToString(info.GetContext(), value));
            });

        /// @description The text color as a game color index.
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

        /// @description The font index used to render the text.
        /// @type {number}
        Property(
            cls, "font",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->font.load());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                drawable->font.store(convert::ToInt32(info.GetContext(), value));
            });
    }
};

}  // namespace d2bs::api::classes
