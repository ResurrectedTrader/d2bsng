#pragma once

#include <v8.h>
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Extract.h"
#include "components/drawing/Drawable.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::runtime::api::classes {

// Import drawing types used by all drawing JS class headers
using drawing::Align;
using drawing::BoxDrawable;
using drawing::Drawable;
using drawing::FrameDrawable;
using drawing::ImageDrawable;
using drawing::LineDrawable;
using drawing::TextDrawable;

// extract is a sibling namespace - alias so all drawing JS headers can write
// extract::PointInto / SizeInto without fully qualifying.
namespace extract = extract;

template <typename Derived, typename DrawableType>
class JSDrawableBase : public ClassBase<Derived, DrawableType> {
   protected:
    using Base = ClassBase<Derived, DrawableType>;

    // Hands the drawable to the script and gives `obj` a cell over it. The script clears the cell when it drops the
    // drawable (remove() or isolate teardown), so the wrapper never reaches a freed drawable.
    static void Adopt(v8::Isolate* isolate, v8::Local<v8::Object> obj, script::Script& script,
                      std::shared_ptr<DrawableType> drawable) {
        auto* cell = Base::Borrow(isolate, obj, drawable.get());
        script.AddDrawable(std::move(drawable), cell, Base::InstanceClassId());
    }

    // The click / hover callbacks are owned by the script, not the drawable -
    // see Script::SetDrawableHandler.
    static void GetHandler(const v8::PropertyCallbackInfo<v8::Value>& info, script::DrawableHandler which) {
        auto* drawable = Base::Unwrap(info.Holder());
        if (!drawable)
            return;
        auto* script = script::ScriptEngine::Instance().GetScript(info.GetIsolate());
        if (!script)
            return;
        v8::Local<v8::Function> handler;
        if (script->GetDrawableHandler(*drawable, which).ToLocal(&handler)) {
            info.GetReturnValue().Set(handler);
        }
    }

    static void SetHandler(const v8::PropertyCallbackInfo<v8::Boolean>& info, script::DrawableHandler which,
                           v8::Local<v8::Value> value) {
        auto* drawable = Base::Unwrap(info.Holder());
        if (!drawable)
            return;
        auto* script = script::ScriptEngine::Instance().GetScript(info.GetIsolate());
        if (!script)
            return;
        script->SetDrawableHandler(*drawable, which,
                                   value->IsFunction() ? value.As<v8::Function>() : v8::Local<v8::Function>());
    }

    static void ConfigureCommonProperties(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> inst,
                                          v8::Local<v8::ObjectTemplate> proto) {
        // x property
        /// @description Horizontal screen position in pixels; may be fractional.
        /// @type {number}
        Base::Property(
            isolate, inst, "x",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->pos.load().x);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->pos.load();
                cur.x = extract::Coordinate(info.GetIsolate(), value);
                drawable->pos.store(cur);
            });

        // y property
        /// @description Vertical screen position in pixels; may be fractional.
        /// @type {number}
        Base::Property(
            isolate, inst, "y",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->pos.load().y);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->pos.load();
                cur.y = extract::Coordinate(info.GetIsolate(), value);
                drawable->pos.store(cur);
            });

        // visible property
        /// @description Whether this overlay is drawn each frame.
        /// @type {boolean}
        Base::Property(
            isolate, inst, "visible",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->isVisible.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                if (!value->IsBoolean())
                    return;
                drawable->isVisible.store(value->BooleanValue(info.GetIsolate()));
            });

        // zorder property
        /// @description Draw order relative to other overlays; higher draws on top.
        /// @type {number}
        Base::Property(
            isolate, inst, "zorder",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->zorder.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                drawable->zorder.store(convert::To<int32_t>(info.GetIsolate(), value));
            });

        // align property
        /// @description Horizontal content alignment: 0 = Left, 1 = Right, 2 = Center.
        /// @type {number}
        Base::Property(
            isolate, inst, "align",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(drawable->align.load()));
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto raw = convert::To<int32_t>(info.GetIsolate(), value);
                if (raw >= 0 && raw <= 2) {
                    drawable->align.store(static_cast<Align>(raw));
                }
            });

        // automap property
        /// @description Whether coordinates are interpreted as automap space instead of screen space.
        /// @type {boolean}
        Base::Property(
            isolate, inst, "automap",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->isAutomap.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* drawable = Base::Unwrap(info.Holder());
                if (!drawable)
                    return;
                if (!value->IsBoolean())
                    return;
                drawable->isAutomap.store(value->BooleanValue(info.GetIsolate()));
            });

        // click property
        /// @description Handler invoked when this overlay is clicked.
        /// @type {function}
        /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
        /// game (the click waits up to 3 seconds for the answer)
        Base::Property(
            isolate, inst, "click",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                GetHandler(info, script::DrawableHandler::Click);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                SetHandler(info, script::DrawableHandler::Click, value);
            });

        // hover property
        /// @description Handler invoked when the mouse hovers over this overlay.
        /// @type {function}
        /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
        /// (entered = false, x and y are 0 on leave); return value ignored
        Base::Property(
            isolate, inst, "hover",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                GetHandler(info, script::DrawableHandler::Hover);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                SetHandler(info, script::DrawableHandler::Hover, value);
            });

        // remove method
        /// @description Unregisters this overlay so it stops rendering.
        /// @signature remove()
        /// @returns {undefined} - No value.
        Base::Method(
            isolate, proto, "remove", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* drawable = Base::Unwrap(args.This());
                if (!drawable)
                    return;
                auto* script = script::ScriptEngine::Instance().GetScript(args.GetIsolate());
                if (!script)
                    return;
                script->RemoveDrawable(drawable->shared_from_this());
            });
    }
};

}  // namespace d2bs::runtime::api::classes
