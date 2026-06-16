// d2bs/api/classes/drawing/JSDrawableBase.h
#pragma once

#include <v8.h>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/core/V8Extract.h"
#include "api/core/V8InstanceTracker.h"
#include "components/drawing/Drawable.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "components/script/ScriptTypes.h"

namespace d2bs::api::classes {

// Import drawing types used by all drawing JS class headers
using d2bs::framework::drawing::Align;
using d2bs::framework::drawing::BoxDrawable;
using d2bs::framework::drawing::Drawable;
using d2bs::framework::drawing::FrameDrawable;
using d2bs::framework::drawing::ImageDrawable;
using d2bs::framework::drawing::LineDrawable;
using d2bs::framework::drawing::TextDrawable;

// v8_extract is a sibling namespace - alias so all drawing JS headers can write
// v8_extract::PointInto / SizeInto without fully qualifying.
namespace v8_extract = d2bs::api::v8_extract;

template <typename Derived, typename DrawableType>
class JSDrawableBase : public V8ClassBase<Derived, DrawableType> {
   protected:
    using Base = V8ClassBase<Derived, DrawableType>;

    // Set up instance tracking for a drawable before Wrap().
    // Increments the per-thread count now and installs an onDestroy hook that
    // decrements when the owning Script destroys the drawable.
    static void SetupInstanceTracking(DrawableType* drawable) {
        V8InstanceTracker::Instance().Increment(Derived::ClassName);
        drawable->onDestroy = [] {
            V8InstanceTracker::Instance().Decrement(Derived::ClassName);
        };
    }

    static void ConfigureCommonProperties(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> inst,
                                          v8::Local<v8::ObjectTemplate> proto) {
        // x property
        /// @description Horizontal screen position in pixels.
        /// @type {number}
        Base::Property(
            isolate, inst, "x",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->pos.load().x);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->pos.load();
                cur.x = v8_convert::ToInt32(info.GetIsolate(), value);
                drawable->pos.store(cur);
            });

        // y property
        /// @description Vertical screen position in pixels.
        /// @type {number}
        Base::Property(
            isolate, inst, "y",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->pos.load().y);
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto cur = drawable->pos.load();
                cur.y = v8_convert::ToInt32(info.GetIsolate(), value);
                drawable->pos.store(cur);
            });

        // visible property
        /// @description Whether this overlay is drawn each frame.
        /// @type {boolean}
        Base::Property(
            isolate, inst, "visible",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->isVisible.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
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
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->zorder.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                drawable->zorder.store(v8_convert::ToInt32(info.GetIsolate(), value));
            });

        // align property
        /// @description Horizontal content alignment: 0 = Left, 1 = Right, 2 = Center.
        /// @type {number}
        Base::Property(
            isolate, inst, "align",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(drawable->align.load()));
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (!value->IsNumber())
                    return;
                auto raw = v8_convert::ToInt32(info.GetIsolate(), value);
                if (raw >= 0 && raw <= 2) {
                    drawable->align.store(static_cast<d2bs::framework::drawing::Align>(raw));
                }
            });

        // automap property
        /// @description Whether coordinates are interpreted as automap space instead of screen space.
        /// @type {boolean}
        Base::Property(
            isolate, inst, "automap",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                info.GetReturnValue().Set(drawable->isAutomap.load());
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
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
        /// game (block votes are not awaited in the current build)
        Base::Property(
            isolate, inst, "click",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (!drawable->onClick.IsEmpty()) {
                    info.GetReturnValue().Set(drawable->onClick.Get(info.GetIsolate()));
                }
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (value->IsFunction()) {
                    drawable->onClick.Reset(info.GetIsolate(), value.As<v8::Function>());
                } else {
                    drawable->onClick.Reset();
                }
            });

        // hover property
        /// @description Handler invoked when the mouse hovers over this overlay.
        /// @type {function}
        /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
        /// (entered = false, x and y are 0 on leave); return value ignored
        Base::Property(
            isolate, inst, "hover",
            +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (!drawable->onHover.IsEmpty()) {
                    info.GetReturnValue().Set(drawable->onHover.Get(info.GetIsolate()));
                }
            },
            +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable)
                    return;
                if (value->IsFunction()) {
                    drawable->onHover.Reset(info.GetIsolate(), value.As<v8::Function>());
                } else {
                    drawable->onHover.Reset();
                }
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
                auto* script = d2bs::ScriptEngine::Instance().GetScript(args.GetIsolate());
                if (!script)
                    return;
                script->RemoveDrawable(drawable->shared_from_this());
                Base::Wrap(args.This(), nullptr);
            });
    }
};

}  // namespace d2bs::api::classes
