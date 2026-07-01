#pragma once

#include "JSDrawableBase.h"

namespace d2bs::api::classes {

class JSFrame : public JSDrawableBase<JSFrame, FrameDrawable> {
   public:
    static constexpr std::string_view ClassName = "Frame";

    /// @description Create a frame overlay.
    /// @signature Frame(x?: number, y?: number, xsize?: number, ysize?: number, align?: number, automap?: boolean,
    /// click?: function, hover?: function)
    /// @param x {number} - left position in pixels
    /// @param y {number} - top position in pixels
    /// @param xsize {number} - width in pixels
    /// @param ysize {number} - height in pixels
    /// @param align {number} - alignment mode
    /// @param automap {boolean} - draw on the automap
    /// @param click {function} - click handler
    /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
    /// game (block votes are not awaited in the current build)
    /// @param hover {function} - hover handler
    /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
    /// (entered = false, x and y are 0 on leave); return value ignored
    /// @returns {Frame}
    /// @throws {Error} - when called outside a running script (no owning script context).
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
        V8_CLASS_CTOR_PROLOGUE;

        auto* script = ScriptEngine::Instance().GetScript(isolate);
        if (!script) {
            v8_error::ThrowError(isolate, "Frame: no owning script");
            return;
        }
        // Framehooks are gated in the game-layer DrawFrame: it no-ops when the
        // client isn't in-game. OOG-script-created frames are therefore silent
        // no-ops at draw time, matching reference behavior (FrameHook always
        // passed IG state).

        auto drawable = std::make_shared<FrameDrawable>();

        v8_extract::PointInto(args, 0, drawable->pos);
        v8_extract::SizeInto(args, 2, drawable->size);

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

        // xsize property
        /// @description Frame width in pixels.
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

        // ysize property
        /// @description Frame height in pixels.
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
    }
};

}  // namespace d2bs::api::classes
