#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "JSPresetUnit.h"
#include "JSUnit.h"
#include "Receiver.h"
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "game/Bridge.h"
#include "game/GameHelpers.h"
#include "game/Room.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Binding for game::Room (map tile).
class JSRoom : public ClassBase<JSRoom, game::Room> {
   public:
    static constexpr std::string_view ClassName = "Room";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description Room-tree room number identifier.
        /// @type {number}
        Property(
            cls, "number", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Number());
            });

        /// @description Room origin X in subtiles (room-tree X position).
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                // reference d2bs parity: room.x/y are exposed as subtiles; Room::Bounds() returns game-coords (see
                // docs/coords.md).
                info.GetReturnValue().Set(data->Bounds().origin.x / 5U);
            });

        /// @description Room origin Y in subtiles (room-tree Y position).
        /// @type {number}
        Property(
            cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                // reference d2bs parity: room.x/y are exposed as subtiles; Room::Bounds() returns game-coords (see
                // docs/coords.md).
                info.GetReturnValue().Set(data->Bounds().origin.y / 5U);
            });

        /// @description Room width.
        /// @type {number}
        Property(
            cls, "xsize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.width);
            });

        /// @description Room height.
        /// @type {number}
        Property(
            cls, "ysize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.height);
            });

        /// @description Room sub-number (secondary room-tree identifier).
        /// @type {number}
        Property(
            cls, "subnumber", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(data->SubNumber());
            });

        /// @description Level/area ID this room belongs to. Alias of the level property.
        /// @type {number}
        Property(
            cls, "area", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->LevelId());
            });

        /// @description Level/area ID number this room belongs to (not a Level object). Alias of the area
        /// property.
        /// @type {number}
        Property(
            cls, "level", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->LevelId());
            });

        /// @description Correct-tomb level number for this room (Tal Rasha's Tombs detection).
        /// @type {number}
        Property(
            cls, "correcttomb", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSRoom>(info);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->CorrectTomb());
            });

        // Methods
        /// @description Advances this Room object in place to the next room in the room-tree chain.
        /// @signature getNext()
        /// @returns {boolean} - true if advanced; false at end of chain or when unresolved.
        Method(
            cls, "getNext", +[](const ub::CallbackInfo& args) {
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                auto next = data->GetNext();
                if (!next) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                *data = next;
                args.GetReturnValue().Set(true);
            });

        /// @description Reveals this room on the automap, optionally drawing preset unit markers.
        /// @signature reveal(drawPresets?: boolean)
        /// @param drawPresets {boolean} - when true also reveals preset unit markers (default false).
        /// @returns {boolean} - true on successful reveal; undefined when game not ready or room unresolved.
        Method(
            cls, "reveal", +[](const ub::CallbackInfo& args) {
                if (!game::IsGameReady()) {
                    return;
                }
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;
                }

                bool drawPresets = false;
                if (args.Length() >= 1 && args[0].IsBoolean()) {
                    drawPresets = convert::ToBool(args.GetContext(), args[0]);
                }

                auto lock = game::Bridge::Lock();
                args.GetReturnValue().Set(data->Reveal(drawPresets));
            });

        /// @description Returns PresetUnit objects in this room, optionally filtered by unit type and class id.
        /// @signature getPresetUnits(type?: number, classId?: number)
        /// @param type {number} - unit type to match; pass -1 or omit to match any type.
        /// @param classId {number} - unit class id to match; pass -1 or omit to match any class.
        /// @returns {PresetUnit[]} - matching PresetUnit objects (possibly empty); undefined when game not ready
        /// or room unresolved.
        Method(
            cls, "getPresetUnits", +[](const ub::CallbackInfo& args) {
                if (!game::IsGameReady()) {
                    return;
                }
                const auto& context = args.GetContext();
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;  // Returns undefined - reference returns undefined when room is null
                }

                // IsUint32 (not IsNumber): scripts pass -1 to mean "no filter" - IsUint32 rejects
                // negative values, leaving the optional as nullopt. IsNumber would accept -1 and
                // store 0xFFFFFFFF as an engaged optional, breaking "no filter" semantics.
                std::optional<uint32_t> nType;
                std::optional<uint32_t> nClass;
                if (args.Length() > 0 && args[0].IsUint32()) {
                    nType = convert::ToUint32(context, args[0]);
                }
                if (args.Length() > 1 && args[1].IsUint32()) {
                    nClass = convert::ToUint32(context, args[1]);
                }

                auto lock = game::Bridge::Lock();
                auto presets = data->GetPresetUnits(nType, nClass);
                auto array = ub::Array::New(context, static_cast<uint32_t>(presets.size()));
                if (!array) {
                    return;
                }

                for (uint32_t i = 0; i < presets.size(); ++i) {
                    auto obj = JSPresetUnit::Wrap(context, std::make_shared<game::PresetUnitInfo>(presets[i]));
                    if (!obj) {
                        error::ThrowError(args.GetIsolate(), "Failed to build preset unit array");
                        return;
                    }
                    if (!array->Set(context, i, *obj).value_or(false)) {
                        return;
                    }
                }

                args.GetReturnValue().Set(*array);
            });

        /// @description Returns the room's collision grid as a 2D array indexed grid[y][x] (outer array is rows).
        /// @signature getCollision()
        /// @returns {number[][]} - rows of collision cell flags; undefined when game not ready or room
        /// unresolved.
        Method(
            cls, "getCollision", +[](const ub::CallbackInfo& args) {
                if (!game::IsGameReady()) {
                    return;
                }
                const auto& context = args.GetContext();
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;  // Returns undefined - reference returns undefined when room is null
                }

                auto lock = game::Bridge::Lock();
                auto collision = data->GetCollision();
                auto& isolate = args.GetIsolate();
                auto outerArray = ub::Array::New(context, static_cast<uint32_t>(collision.size()));
                if (!outerArray) {
                    return;
                }

                for (uint32_t y = 0; y < collision.size(); ++y) {
                    ub::HandleScope rowScope(isolate);
                    const auto& row = collision[y];
                    auto innerArray = ub::Array::New(context, static_cast<uint32_t>(row.size()));
                    if (!innerArray) {
                        return;
                    }
                    for (uint32_t x = 0; x < row.size(); ++x) {
                        if (!innerArray->Set(context, x, convert::ToJS(isolate, row[x])).value_or(false)) {
                            return;
                        }
                    }
                    if (!outerArray->Set(context, y, *innerArray).value_or(false)) {
                        return;
                    }
                }

                args.GetReturnValue().Set(*outerArray);
            });

        /// @description Returns the room's collision grid as a flat row-major Uint16Array (length width*height);
        /// faster than getCollision() for bulk access.
        /// @signature getCollisionA()
        /// @returns {Uint16Array} - flat row-major collision cell flags; undefined when game not ready, room
        /// unresolved, or no collision data.
        Method(
            cls, "getCollisionA", +[](const ub::CallbackInfo& args) {
                if (!game::IsGameReady()) {
                    return;
                }
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;  // Returns undefined - reference returns undefined when room is null
                }

                auto flat = data->GetCollisionFlat();
                if (flat.empty()) {
                    return;  // Returns undefined - reference returns undefined when collision data is null
                }
                if (auto view = ub::TypedArray::New(args.GetContext(), std::span<const uint16_t>(flat))) {
                    args.GetReturnValue().Set(*view);
                }
            });

        /// @description Returns Room objects adjacent to / near this room.
        /// @signature getNearby()
        /// @returns {Room[]} - nearby Room objects; empty array when unresolved or none.
        Method(
            cls, "getNearby", +[](const ub::CallbackInfo& args) {
                const auto& context = args.GetContext();
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
                    if (auto empty = ub::Array::New(context)) {
                        args.GetReturnValue().Set(*empty);
                    }
                    return;
                }

                auto nearby = data->GetNearby();
                auto array = ub::Array::New(context, static_cast<uint32_t>(nearby.size()));
                if (!array) {
                    return;
                }

                for (uint32_t i = 0; i < nearby.size(); ++i) {
                    auto obj = Wrap(context, std::make_shared<game::Room>(nearby[i]));
                    if (!obj) {
                        error::ThrowError(args.GetIsolate(), "Failed to build nearby room array");
                        return;
                    }
                    if (!array->Set(context, i, *obj).value_or(false)) {
                        return;
                    }
                }

                args.GetReturnValue().Set(*array);
            });

        /// @description Reads a raw room field by stat index; the returned field varies per index, in mixed
        /// units (subtiles vs game-coords - see docs/coords.md). Accepted indices (others, including 8,
        /// return 0): 0 = collision xStart, 1 = yStart, 2 = xSize, 3 = ySize, 4 = room-tree posX, 5 = posY,
        /// 6 = sizeX, 7 = sizeY, 9 = collision posGameX, 10 = posGameY, 11 = sizeGameX, 12 = sizeGameY,
        /// 13 = posRoomX, 14 = posRoomY, 15 = sizeRoomX, 16 = sizeRoomY.
        /// @signature getStat(index: number)
        /// @param index {number} - stat index 0-16 (see @description; 8 and out-of-range yield 0).
        /// @returns {number|null} - raw stat value (0 for unmapped/out-of-range/index-8); null only when not
        /// ready, bad args, or room unresolved.
        Method(
            cls, "getStat", +[](const ub::CallbackInfo& args) {
                args.GetReturnValue().SetNull();
                if (!game::IsGameReady()) {
                    return;
                }
                if (args.Length() < 1 || !args[0].IsNumber()) {
                    return;
                }
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;
                }
                int32_t nStat = convert::ToInt32(args.GetContext(), args[0]);
                auto lock = game::Bridge::Lock();
                args.GetReturnValue().Set(data->GetStat(nStat));
            });

        /// @description Returns a new Room object for the first room in this room's level (does not mutate this
        /// object, unlike getNext).
        /// @signature getFirst()
        /// @returns {Room} - the first Room in the level; undefined when unresolved or none.
        Method(
            cls, "getFirst", +[](const ub::CallbackInfo& args) {
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;
                }
                auto first = data->GetFirst();
                if (!first) {
                    return;
                }
                if (auto obj = Wrap(args.GetContext(), std::make_shared<game::Room>(first))) {
                    args.GetReturnValue().Set(*obj);
                }
            });

        /// @description Tests whether the given Unit is currently in this room.
        /// @signature unitInRoom(unit: Unit)
        /// @param unit {Unit} - the Unit object to test.
        /// @returns {boolean} - true if the unit is in this room, false otherwise; undefined on invalid/missing
        /// argument or unresolved handles.
        Method(
            cls, "unitInRoom", +[](const ub::CallbackInfo& args) {
                if (args.Length() < 1 || !args[0].IsObject()) {
                    return;  // Returns undefined, matching reference early-return
                }
                auto* data = Receiver<JSRoom>(args);
                if (data == nullptr || !*data) {
                    return;  // undefined, matching reference
                }

                auto* unitData = JSUnit::Unwrap(args[0]);
                if (unitData == nullptr || !*unitData) {
                    return;  // undefined, matching reference
                }

                args.GetReturnValue().Set(unitData->GetRoom() == *data);
            });
    }
};

}  // namespace d2bs::api::classes
