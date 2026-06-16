#pragma once

#include <v8.h>
#include "api/core/V8Class.h"
#include "api/core/V8Error.h"
#include "game/Room.h"

namespace d2bs::api::classes {

// PresetUnit class - represents a preset unit in the game
// Preset units are static objects placed in rooms at level generation
// (e.g., waypoints, shrines, quest objects, special monsters)
class JSPresetUnit : public V8ClassBase<JSPresetUnit, d2bs::game::PresetUnitInfo> {
   public:
    static constexpr std::string_view ClassName = "PresetUnit";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        /// @description Unit type code identifying the kind of preset object.
        /// 1 = monster, 2 = object, 5 = tile.
        /// @type {number}
        Property(
            isolate, inst, "type", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->type);
            });

        /// @description X coordinate of the containing room in room-grid units.
        /// @type {number}
        Property(
            isolate, inst, "roomx", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->roomPos.x);
            });

        /// @description Y coordinate of the containing room in room-grid units.
        /// @type {number}
        Property(
            isolate, inst, "roomy", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->roomPos.y);
            });

        /// @description X position within the room, in game world coordinates (see docs/coords.md).
        /// @type {number}
        Property(
            isolate, inst, "x", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->posInRoom.x);
            });

        /// @description Y position within the room, in game world coordinates (see docs/coords.md).
        /// @type {number}
        Property(
            isolate, inst, "y", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->posInRoom.y);
            });

        /// @description Class ID of the specific unit (object, monster, or tile ID).
        /// @type {number}
        Property(
            isolate, inst, "id", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->id);
            });

        /// @description Level (area) number where the preset unit resides.
        /// @type {number}
        Property(
            isolate, inst, "level", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                info.GetReturnValue().Set(data->level);
            });
    }
};

}  // namespace d2bs::api::classes
