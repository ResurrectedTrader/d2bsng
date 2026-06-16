// d2bs/api/classes/drawing/JSText.h
#pragma once

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
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
        V8_CLASS_CTOR_PROLOGUE;

        auto* script = d2bs::ScriptEngine::Instance().GetScript(isolate);
        if (!script) {
            v8_error::ThrowError(isolate, "Text: no owning script");
            return;
        }

        auto drawable = std::make_shared<TextDrawable>();

        if (args.Length() > 0 && args[0]->IsString()) {
            drawable->SetText(v8_convert::ToString(isolate, args[0]));
        }
        v8_extract::PointInto(args, 1, drawable->pos);

        if (args.Length() > 3 && args[3]->IsNumber()) {
            drawable->color.store(v8_convert::ToUint32(isolate, args[3]));
        }
        if (args.Length() > 4 && args[4]->IsNumber()) {
            drawable->font.store(v8_convert::ToInt32(isolate, args[4]));
        }
        if (args.Length() > 5 && args[5]->IsNumber()) {
            drawable->align.store(static_cast<Align>(v8_convert::ToInt32(isolate, args[5])));
        }
        if (args.Length() > 6 && args[6]->IsBoolean()) {
            drawable->isAutomap.store(args[6]->BooleanValue(isolate));
        }
        if (args.Length() > 7 && args[7]->IsFunction()) {
            drawable->onClick.Reset(isolate, args[7].As<v8::Function>());
        }
        if (args.Length() > 8 && args[8]->IsFunction()) {
            drawable->onHover.Reset(isolate, args[8].As<v8::Function>());
        }

        auto* rawDrawable = drawable.get();
        SetupInstanceTracking(rawDrawable);
        script->AddDrawable(std::move(drawable));

        Wrap(args.This(), rawDrawable);
        args.GetReturnValue().Set(args.This());
    }

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        ConfigureCommonProperties(isolate, inst, proto);

        /// @description The displayed text string of this Text overlay.
        /// @type {string}
        Property(
            isolate, inst, "text",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), drawable->GetText()));
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsString())
                    return;
                drawable->SetText(v8_convert::ToString(info.GetIsolate(), value));
            });

        /// @description The text color as a game color index.
        /// @type {number}
        Property(
            isolate, inst, "color",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->color.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                drawable->color.store(v8_convert::ToUint32(info.GetIsolate(), value));
            });

        /// @description The font index used to render the text.
        /// @type {number}
        Property(
            isolate, inst, "font",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->font.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                drawable->font.store(v8_convert::ToInt32(info.GetIsolate(), value));
            });
    }
};

}  // namespace d2bs::api::classes
