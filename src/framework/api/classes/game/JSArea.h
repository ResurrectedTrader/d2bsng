#pragma once

#include <v8.h>
#include "JSExit.h"
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "game/Level.h"

namespace d2bs::api::classes {

// Area class - represents a game area/level
// Areas contain rooms and provide information about level layout
class JSArea : public V8ClassBase<JSArea, d2bs::game::Level> {
   public:
    static constexpr std::string_view ClassName = "Area";

    // Area objects are obtained via getArea() global function, not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        /// @description Exits leading out of this area, each as an Exit object.
        /// @type {Array<Exit>}
        Property(
            isolate, inst, "exits", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(v8::Array::New(info.GetIsolate(), 0));
                    return;
                }

                auto* isolate = info.GetIsolate();

                auto exits = data->GetExits();
                auto context = isolate->GetCurrentContext();
                auto array = v8::Array::New(isolate, static_cast<int32_t>(exits.size()));

                for (uint32_t i = 0; i < exits.size(); ++i) {
                    auto exitObj =
                        JSExit::CreateInstance(isolate, context, std::make_unique<d2bs::game::ExitInfo>(exits[i]));
                    if (exitObj.IsEmpty()) {
                        v8_error::ThrowError(isolate, "Failed to build exit array");
                        return;
                    }
                    array->Set(context, i, exitObj).Check();
                }

                info.GetReturnValue().Set(array);
            });

        /// @description Level number identifying this area, matching the Areas constant.
        /// @type {number}
        Property(
            isolate, inst, "id", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->Id()));
            });

        /// @description Human-readable level name for this area.
        /// @type {string}
        Property(
            isolate, inst, "name", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                auto* isolate = info.GetIsolate();
                if (!*data) {
                    info.GetReturnValue().SetEmptyString();
                    return;
                }
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->Name()));
            });

        // reference d2bs parity: area.x/y/xsize/ysize are exposed as subtiles; Level::Bounds() returns game-coords (see
        // docs/coords.md).
        /// @description X origin of the area in subtiles.
        /// @type {number}
        Property(
            isolate, inst, "x", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().origin.x / 5U);
            });

        /// @description Y origin of the area in subtiles.
        /// @type {number}
        Property(
            isolate, inst, "y", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().origin.y / 5U);
            });

        /// @description Width of the area in subtiles.
        /// @type {number}
        Property(
            isolate, inst, "xsize", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.width / 5U);
            });

        /// @description Height of the area in subtiles.
        /// @type {number}
        Property(
            isolate, inst, "ysize", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.height / 5U);
            });
    }
};

}  // namespace d2bs::api::classes
