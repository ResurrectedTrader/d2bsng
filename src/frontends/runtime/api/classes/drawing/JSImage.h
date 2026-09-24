#pragma once

#include <memory>

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
    static std::shared_ptr<ImageDrawable> New(const ub::CallbackInfo& args) {
        const auto& context = args.GetContext();

        auto* script = ScriptEngine::Instance().GetScript(&args.GetIsolate());
        if (!script) {
            error::ThrowError(args.GetIsolate(), "Image: no owning script");
            return nullptr;
        }

        auto drawable = std::make_shared<ImageDrawable>();

        if (args[0].IsString()) {
            drawable->SetPath(convert::ToString(context, args[0]));
        }
        extract::PointInto(args, 1, drawable->pos);
        if (args[3].IsNumber()) {
            drawable->color.store(convert::ToUint32(context, args[3]));
        }
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

    static void Configure(const ub::Class<ImageDrawable>& cls) {
        ConfigureCommonProperties(cls);

        /// @description The sprite resource path the image renders.
        /// @type {string}
        Property(
            cls, "location",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable) {
                    return;
                }
                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), drawable->GetPath()));
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* drawable = Unwrap(info.This());
                if (!drawable || !value.IsString()) {
                    return;
                }
                drawable->SetPath(convert::ToString(info.GetContext(), value));
            });
    }
};

}  // namespace d2bs::api::classes
