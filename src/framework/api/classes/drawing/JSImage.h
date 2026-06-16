// d2bs/api/classes/drawing/JSImage.h
#pragma once

#include "JSDrawableBase.h"

namespace d2bs::api::classes {

class JSImage : public JSDrawableBase<JSImage, ImageDrawable> {
   public:
    static constexpr std::string_view ClassName = "Image";

    /// @description Creates a screen-overlay image drawable rendering a sprite at a screen position.
    /// @signature Image(path?: string, x?: number, y?: number, color?: number, align?: number, automap?: boolean,
    /// click?: function, hover?: function)
    /// @param path {string} - Sprite resource path, resolved relative to the script then as an absolute path.
    /// @param x {number} - Screen x coordinate.
    /// @param y {number} - Screen y coordinate.
    /// @param color {number} - Palette tint color index.
    /// @param align {number} - Horizontal alignment: 0 left, 1 right, 2 center.
    /// @param automap {boolean} - When true, the position is in automap space and follows the automap.
    /// @param click {function} - Handler invoked when the image is clicked.
    /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
    /// game (block votes are not awaited in the current build)
    /// @param hover {function} - Handler invoked when the cursor is over the image.
    /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
    /// (entered = false, x and y are 0 on leave); return value ignored
    /// @returns {Image}
    /// @throws {Error} - when called outside a running script (no owning script context).
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
        V8_CLASS_CTOR_PROLOGUE;

        auto* script = d2bs::ScriptEngine::Instance().GetScript(isolate);
        if (!script) {
            v8_error::ThrowError(isolate, "Image: no owning script");
            return;
        }

        auto drawable = std::make_shared<ImageDrawable>();

        if (args.Length() > 0 && args[0]->IsString()) {
            drawable->SetPath(v8_convert::ToString(isolate, args[0]));
        }
        v8_extract::PointInto(args, 1, drawable->pos);
        if (args.Length() > 3 && args[3]->IsNumber()) {
            drawable->color.store(v8_convert::ToUint32(isolate, args[3]));
        }
        if (args.Length() > 4 && args[4]->IsNumber()) {
            drawable->align.store(static_cast<Align>(v8_convert::ToInt32(isolate, args[4])));
        }
        if (args.Length() > 5 && args[5]->IsBoolean()) {
            drawable->isAutomap.store(args[5]->BooleanValue(isolate));
        }
        if (args.Length() > 6 && args[6]->IsFunction()) {
            drawable->onClick.Reset(isolate, args[6].As<v8::Function>());
        }
        if (args.Length() > 7 && args[7]->IsFunction()) {
            drawable->onHover.Reset(isolate, args[7].As<v8::Function>());
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

        /// @description The sprite resource path the image renders.
        /// @type {string}
        Property(
            isolate, inst, "location",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), drawable->GetPath()));
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsString())
                    return;
                drawable->SetPath(v8_convert::ToString(info.GetIsolate(), value));
            });
    }
};

}  // namespace d2bs::api::classes
