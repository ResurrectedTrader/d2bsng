#pragma once

#include <string_view>

#include "Receiver.h"
#include "api/core/Class.h"
#include "game/Types.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// PresetUnit class - represents a preset unit in the game
// Preset units are static objects placed in rooms at level generation
// (e.g., waypoints, shrines, quest objects, special monsters)
class JSPresetUnit : public ClassBase<JSPresetUnit, game::PresetUnitInfo> {
   public:
    static constexpr std::string_view ClassName = "PresetUnit";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description Unit type code identifying the kind of preset object.
        /// 1 = monster, 2 = object, 5 = tile.
        /// @type {number}
        Property(
            cls, "type", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->type);
            });

        /// @description X coordinate of the containing room in room-grid units.
        /// @type {number}
        Property(
            cls, "roomx", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->roomPos.x);
            });

        /// @description Y coordinate of the containing room in room-grid units.
        /// @type {number}
        Property(
            cls, "roomy", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->roomPos.y);
            });

        /// @description X position within the room, in game world coordinates (see docs/coords.md).
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->posInRoom.x);
            });

        /// @description Y position within the room, in game world coordinates (see docs/coords.md).
        /// @type {number}
        Property(
            cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->posInRoom.y);
            });

        /// @description Class ID of the specific unit (object, monster, or tile ID).
        /// @type {number}
        Property(
            cls, "id", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->id);
            });

        /// @description Level (area) number where the preset unit resides.
        /// @type {number}
        Property(
            cls, "level", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSPresetUnit>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->level);
            });
    }
};

}  // namespace d2bs::api::classes
