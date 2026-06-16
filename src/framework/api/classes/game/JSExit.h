#pragma once

#include <v8.h>
#include "api/core/V8Class.h"
#include "api/core/V8Error.h"
#include "game/Level.h"

namespace d2bs::api::classes {

// Exit class - represents an exit point from one area to another
// Exits are obtained from Area.exits property
class JSExit : public V8ClassBase<JSExit, d2bs::game::ExitInfo> {
   public:
    static constexpr std::string_view ClassName = "Exit";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        /// @description The exit's X coordinate in world coordinates.
        /// @type {number}
        Property(
            isolate, inst, "x", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->pos.x);
            });

        /// @description The exit's Y coordinate in world coordinates.
        /// @type {number}
        Property(
            isolate, inst, "y", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->pos.y);
            });

        /// @description The destination this exit leads to, interpreted according to `type`.
        /// @type {number}
        Property(
            isolate, inst, "target", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->target);
            });

        /// @description The kind of exit: 1 (linkage) or 2 (tile).
        /// @type {number}
        Property(
            isolate, inst, "type", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(static_cast<uint32_t>(data->type));
            });

        /// @description The tile id associated with this exit.
        /// @type {number}
        Property(
            isolate, inst, "tileid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->tileId);
            });

        /// @description The level number this exit belongs to.
        /// @type {number}
        Property(
            isolate, inst, "level", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->level);
            });
    }
};

}  // namespace d2bs::api::classes
