#pragma once

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
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args) {
        V8_CLASS_CTOR_PROLOGUE;

        auto* script = ScriptEngine::Instance().GetScript(isolate);
        if (!script) {
            v8_error::ThrowError(isolate, "Line: no owning script");
            return;
        }

        auto drawable = std::make_shared<LineDrawable>();

        v8_extract::PointInto(args, 0, drawable->pos);
        v8_extract::PointInto(args, 2, drawable->p2);

        if (args.Length() > 4 && args[4]->IsNumber()) {
            drawable->color.store(v8_convert::ToUint32(isolate, args[4]));
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

        /// @description X coordinate of the line's second endpoint
        /// @type {number}
        Property(
            isolate, inst, "x2",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->p2.load().x);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->p2.load();
                cur.x = v8_convert::ToInt32(info.GetIsolate(), value);
                drawable->p2.store(cur);
            });

        /// @description Y coordinate of the line's second endpoint
        /// @type {number}
        Property(
            isolate, inst, "y2",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->p2.load().y);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->p2.load();
                cur.y = v8_convert::ToInt32(info.GetIsolate(), value);
                drawable->p2.store(cur);
            });

        /// @description Line draw color as a game color palette index
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
    }
};

}  // namespace d2bs::api::classes
