// d2bs/api/classes/drawing/JSBox.h
#pragma once

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
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
        V8_CLASS_CTOR_PROLOGUE;

        auto* script = d2bs::ScriptEngine::Instance().GetScript(isolate);
        if (!script) {
            v8_error::ThrowError(isolate, "Box: no owning script");
            return;
        }

        auto drawable = std::make_shared<BoxDrawable>();

        v8_extract::PointInto(args, 0, drawable->pos);
        v8_extract::SizeInto(args, 2, drawable->size);

        if (args.Length() > 4 && args[4]->IsNumber()) {
            drawable->color.store(v8_convert::ToUint32(isolate, args[4]));
        }
        if (args.Length() > 5 && args[5]->IsNumber()) {
            drawable->opacity.store(v8_convert::ToUint32(isolate, args[5]));
        }
        if (args.Length() > 6 && args[6]->IsNumber()) {
            drawable->align.store(static_cast<Align>(v8_convert::ToInt32(isolate, args[6])));
        }
        if (args.Length() > 7 && args[7]->IsBoolean()) {
            drawable->isAutomap.store(args[7]->BooleanValue(isolate));
        }
        if (args.Length() > 8 && args[8]->IsFunction()) {
            drawable->onClick.Reset(isolate, args[8].As<v8::Function>());
        }
        if (args.Length() > 9 && args[9]->IsFunction()) {
            drawable->onHover.Reset(isolate, args[9].As<v8::Function>());
        }

        auto* rawDrawable = drawable.get();
        SetupInstanceTracking(rawDrawable);
        script->AddDrawable(std::move(drawable));

        Wrap(args.This(), rawDrawable);
        // The owning Script holds the shared_ptr. rawDrawable stays valid
        // until remove() or TeardownIsolate drops that ref.
        args.GetReturnValue().Set(args.This());
    }

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        ConfigureCommonProperties(isolate, inst, proto);

        /// @description Box width in pixels (alias for the width property).
        /// @type {number}
        Property(
            isolate, inst, "xsize",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->size.load().width);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->size.load();
                // Note: Size is unsigned; negative values coerce to large positive via ToUint32.
                cur.width = v8_convert::ToUint32(info.GetIsolate(), value);
                drawable->size.store(cur);
            });

        /// @description Box height in pixels (alias for the height property).
        /// @type {number}
        Property(
            isolate, inst, "ysize",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->size.load().height);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->size.load();
                // Note: Size is unsigned; negative values coerce to large positive via ToUint32.
                cur.height = v8_convert::ToUint32(info.GetIsolate(), value);
                drawable->size.store(cur);
            });

        /// @description Box fill color as a game color index.
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

        /// @description Box fill opacity.
        /// @type {number}
        Property(
            isolate, inst, "opacity",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->opacity.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                drawable->opacity.store(v8_convert::ToUint32(info.GetIsolate(), value));
            });
    }
};

}  // namespace d2bs::api::classes
