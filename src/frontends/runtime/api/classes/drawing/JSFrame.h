#pragma once

#include <memory>

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
    static std::shared_ptr<FrameDrawable> New(const ub::CallbackInfo& args) {
        const auto& context = args.GetContext();

        auto* script = ScriptEngine::Instance().GetScript(&args.GetIsolate());
        if (!script) {
            error::ThrowError(args.GetIsolate(), "Frame: no owning script");
            return nullptr;
        }
        // Framehooks are gated in the game-layer DrawFrame: it no-ops when the
        // client isn't in-game. OOG-script-created frames are therefore silent
        // no-ops at draw time, matching reference behavior (FrameHook always
        // passed IG state).

        auto drawable = std::make_shared<FrameDrawable>();

        extract::PointInto(args, 0, drawable->pos);
        extract::SizeInto(args, 2, drawable->size);

        if (args[4].IsNumber()) {
            drawable->align.store(static_cast<Align>(convert::ToInt32(context, args[4])));
        }
        if (args[5].IsBoolean()) {
            drawable->isAutomap.store(args[5].IsTrue());
        }
        SetConstructorHandlers(args, *script, *drawable, 6);

        script->AddDrawable(drawable);
        return drawable;
    }

    static void Configure(const ub::Class<FrameDrawable>& cls) {
        ConfigureCommonProperties(cls);

        // xsize property
        /// @description Frame width in pixels.
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

        // ysize property
        /// @description Frame height in pixels.
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
    }
};

}  // namespace d2bs::api::classes
