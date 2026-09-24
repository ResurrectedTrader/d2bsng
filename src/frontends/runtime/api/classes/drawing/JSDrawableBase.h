#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/core/Extract.h"
#include "components/drawing/Drawable.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Import drawing types used by all drawing JS class headers
using runtime::drawing::Align;
using runtime::drawing::BoxDrawable;
using runtime::drawing::Drawable;
using runtime::drawing::FrameDrawable;
using runtime::drawing::ImageDrawable;
using runtime::drawing::LineDrawable;
using runtime::drawing::TextDrawable;

// Instances hold a share of their drawable, as does the owning Script until remove() or teardown.
template <typename Derived, typename DrawableType>
class JSDrawableBase : public ClassBase<Derived, DrawableType> {
   protected:
    using Base = ClassBase<Derived, DrawableType>;

    // Whether `script` still owns `drawable`. A wrapper keeps its drawable alive past remove(), and
    // a handler installed on a removed drawable would be keyed on its address after it is freed.
    static bool IsAttached(Script& script, const Drawable& drawable) {
        return std::ranges::any_of(script.GetDrawables(),
                                   [&drawable](const auto& owned) { return owned.get() == &drawable; });
    }

    // The click / hover callbacks are owned by the script, not the drawable -
    // see Script::SetDrawableHandler.
    static void GetHandler(const ub::PropertyCallbackInfo& info, DrawableHandler which) {
        auto* drawable = Base::Unwrap(info.This());
        if (!drawable) {
            return;
        }
        auto* script = ScriptEngine::Instance().GetScript(&info.GetIsolate());
        if (!script) {
            return;
        }
        if (auto handler = script->GetDrawableHandler(*drawable, which)) {
            info.GetReturnValue().Set(*handler);
        }
    }

    static void SetHandler(const ub::PropertyCallbackInfo& info, DrawableHandler which,
                           const ub::Local<ub::Value>& value) {
        auto* drawable = Base::Unwrap(info.This());
        if (!drawable) {
            return;
        }
        auto* script = ScriptEngine::Instance().GetScript(&info.GetIsolate());
        if (!script || !IsAttached(*script, *drawable)) {
            return;
        }
        script->SetDrawableHandler(*drawable, which, value.To<ub::Function>().value_or(ub::Local<ub::Function>()));
    }

    static void ConfigureCommonProperties(const ub::Class<DrawableType>& cls) {
        // x property
        /// @description Horizontal screen position in pixels.
        /// @type {number}
        Base::Property(
            cls, "x",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->pos.load().x);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                auto cur = drawable->pos.load();
                cur.x = convert::ToInt32(info.GetContext(), value);
                drawable->pos.store(cur);
            });

        // y property
        /// @description Vertical screen position in pixels.
        /// @type {number}
        Base::Property(
            cls, "y",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->pos.load().y);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                auto cur = drawable->pos.load();
                cur.y = convert::ToInt32(info.GetContext(), value);
                drawable->pos.store(cur);
            });

        // visible property
        /// @description Whether this overlay is drawn each frame.
        /// @type {boolean}
        Base::Property(
            cls, "visible",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->isVisible.load());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable || !value.IsBoolean()) {
                    return;
                }
                drawable->isVisible.store(value.IsTrue());
            });

        // zorder property
        /// @description Draw order relative to other overlays; higher draws on top.
        /// @type {number}
        Base::Property(
            cls, "zorder",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->zorder.load());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                drawable->zorder.store(convert::ToInt32(info.GetContext(), value));
            });

        // align property
        /// @description Horizontal content alignment: 0 = Left, 1 = Right, 2 = Center.
        /// @type {number}
        Base::Property(
            cls, "align",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(drawable->align.load()));
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable || !value.IsNumber()) {
                    return;
                }
                const auto raw = convert::ToInt32(info.GetContext(), value);
                if (raw >= 0 && raw <= 2) {
                    drawable->align.store(static_cast<Align>(raw));
                }
            });

        // automap property
        /// @description Whether coordinates are interpreted as automap space instead of screen space.
        /// @type {boolean}
        Base::Property(
            cls, "automap",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(drawable->isAutomap.load());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Base::Unwrap(info.This());
                if (!drawable || !value.IsBoolean()) {
                    return;
                }
                drawable->isAutomap.store(value.IsTrue());
            });

        // click property
        /// @description Handler invoked when this overlay is clicked.
        /// @type {function}
        /// @callback click(button: number, x: number, y: number) -> {boolean} - return true to block the click from the
        /// game (block votes are not awaited in the current build)
        Base::Property(
            cls, "click",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                GetHandler(info, DrawableHandler::Click);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                SetHandler(info, DrawableHandler::Click, value);
            });

        // hover property
        /// @description Handler invoked when the mouse hovers over this overlay.
        /// @type {function}
        /// @callback hover(x: number, y: number, entered: boolean) - fired on cursor enter (entered = true) and leave
        /// (entered = false, x and y are 0 on leave); return value ignored
        Base::Property(
            cls, "hover",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                GetHandler(info, DrawableHandler::Hover);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                SetHandler(info, DrawableHandler::Hover, value);
            });

        // remove method
        /// @description Unregisters this overlay so it stops rendering.
        /// @signature remove()
        /// @returns {undefined} - No value.
        Base::Method(
            cls, "remove", +[](const ub::CallbackInfo& args) {
                auto* drawable = Base::Unwrap(args.This());
                if (!drawable) {
                    return;
                }
                auto* script = ScriptEngine::Instance().GetScript(&args.GetIsolate());
                if (!script) {
                    return;
                }
                script->RemoveDrawable(drawable->shared_from_this());
            });
    }

    // Installs the click / hover handlers a constructor was passed at `clickIdx` / `clickIdx + 1`.
    static void SetConstructorHandlers(const ub::CallbackInfo& args, Script& script, Drawable& drawable,
                                       uint32_t clickIdx) {
        if (auto click = args[clickIdx].To<ub::Function>()) {
            script.SetDrawableHandler(drawable, DrawableHandler::Click, *click);
        }
        if (auto hover = args[clickIdx + 1].To<ub::Function>()) {
            script.SetDrawableHandler(drawable, DrawableHandler::Hover, *hover);
        }
    }
};

}  // namespace d2bs::api::classes
