#pragma once

#include <cstdint>
#include <cstring>

#include <v8.h>

#include "JSPresetUnit.h"
#include "JSUnit.h"
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "game/Bridge.h"
#include "game/GameHelpers.h"
#include "game/Room.h"

namespace d2bs::api::classes {

// V8 binding for game::Room (map tile).
class JSRoom : public V8ClassBase<JSRoom, d2bs::game::Room> {
   public:
    static constexpr std::string_view ClassName = "Room";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        /// @description Room-tree room number identifier.
        /// @type {number}
        Property(
            isolate, inst, "number", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Number());
            });

        /// @description Room origin X in subtiles (room-tree X position).
        /// @type {number}
        Property(
            isolate, inst, "x", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                // reference d2bs parity: room.x/y are exposed as subtiles; Room::Bounds() returns game-coords (see
                // docs/coords.md).
                info.GetReturnValue().Set(data->Bounds().origin.x / 5U);
            });

        /// @description Room origin Y in subtiles (room-tree Y position).
        /// @type {number}
        Property(
            isolate, inst, "y", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                // reference d2bs parity: room.x/y are exposed as subtiles; Room::Bounds() returns game-coords (see
                // docs/coords.md).
                info.GetReturnValue().Set(data->Bounds().origin.y / 5U);
            });

        /// @description Room width.
        /// @type {number}
        Property(
            isolate, inst, "xsize", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.width);
            });

        /// @description Room height.
        /// @type {number}
        Property(
            isolate, inst, "ysize", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Bounds().size.height);
            });

        /// @description Room sub-number (secondary room-tree identifier).
        /// @type {number}
        Property(
            isolate, inst, "subnumber", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(data->SubNumber());
            });

        /// @description Level/area ID this room belongs to. Alias of the level property.
        /// @type {number}
        Property(
            isolate, inst, "area", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
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
            isolate, inst, "level", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    info.GetReturnValue().Set(0);
                    return;
                }
                info.GetReturnValue().Set(data->LevelId());
            });

        /// @description Correct-tomb level number for this room (Tal Rasha's Tombs detection).
        /// @type {number}
        Property(
            isolate, inst, "correcttomb", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
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
            isolate, proto, "getNext", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* data = Unwrap(args.This());
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
            isolate, proto, "reveal", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                if (!d2bs::game::IsGameReady()) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;
                }

                bool drawPresets = false;
                if (args.Length() >= 1 && args[0]->IsBoolean()) {
                    drawPresets = args[0]->BooleanValue(args.GetIsolate());
                }

                auto lock = d2bs::game::Bridge::Lock();
                args.GetReturnValue().Set(data->Reveal(drawPresets));
            });

        /// @description Returns PresetUnit objects in this room, optionally filtered by unit type and class id.
        /// @signature getPresetUnits(type?: number, classId?: number)
        /// @param type {number} - unit type to match; pass -1 or omit to match any type.
        /// @param classId {number} - unit class id to match; pass -1 or omit to match any class.
        /// @returns {PresetUnit[]} - matching PresetUnit objects (possibly empty); undefined when game not ready
        /// or room unresolved.
        Method(
            isolate, proto, "getPresetUnits", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!d2bs::game::IsGameReady()) {
                    return;
                }
                auto context = isolate->GetCurrentContext();
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;  // Returns undefined - reference returns undefined when room is null
                }

                // IsUint32 (not IsNumber): scripts pass -1 to mean "no filter" - IsUint32 rejects
                // negative values, leaving the optional as nullopt. IsNumber would accept -1 and
                // store 0xFFFFFFFF as an engaged optional, breaking "no filter" semantics.
                std::optional<uint32_t> nType;
                std::optional<uint32_t> nClass;
                if (args.Length() > 0 && args[0]->IsUint32()) {
                    nType = v8_convert::ToUint32(isolate, args[0]);
                }
                if (args.Length() > 1 && args[1]->IsUint32()) {
                    nClass = v8_convert::ToUint32(isolate, args[1]);
                }

                auto lock = d2bs::game::Bridge::Lock();
                auto presets = data->GetPresetUnits(nType, nClass);
                auto array = v8::Array::New(isolate, static_cast<int32_t>(presets.size()));

                for (uint32_t i = 0; i < presets.size(); ++i) {
                    auto puData = std::make_unique<d2bs::game::PresetUnitInfo>(presets[i]);
                    auto obj = JSPresetUnit::CreateInstance(isolate, context, std::move(puData));
                    if (obj.IsEmpty()) {
                        v8_error::ThrowError(isolate, "Failed to build preset unit array");
                        return;
                    }
                    array->Set(context, i, obj).Check();
                }

                args.GetReturnValue().Set(array);
            });

        /// @description Returns the room's collision grid as a 2D array indexed grid[y][x] (outer array is rows).
        /// @signature getCollision()
        /// @returns {number[][]} - rows of collision cell flags; undefined when game not ready or room
        /// unresolved.
        Method(
            isolate, proto, "getCollision", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!d2bs::game::IsGameReady()) {
                    return;
                }
                auto context = isolate->GetCurrentContext();
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;  // Returns undefined - reference returns undefined when room is null
                }

                auto lock = d2bs::game::Bridge::Lock();
                auto collision = data->GetCollision();
                auto outerArray = v8::Array::New(isolate, static_cast<int32_t>(collision.size()));

                for (size_t y = 0; y < collision.size(); ++y) {
                    v8::HandleScope rowScope(isolate);
                    const auto& row = collision[y];
                    auto innerArray = v8::Array::New(isolate, static_cast<int32_t>(row.size()));
                    for (size_t x = 0; x < row.size(); ++x) {
                        innerArray->Set(context, x, v8_convert::ToV8(isolate, row[x])).Check();
                    }
                    outerArray->Set(context, y, innerArray).Check();
                }

                args.GetReturnValue().Set(outerArray);
            });

        /// @description Returns the room's collision grid as a flat row-major Uint16Array (length width*height);
        /// faster than getCollision() for bulk access.
        /// @signature getCollisionA()
        /// @returns {Uint16Array} - flat row-major collision cell flags; undefined when game not ready, room
        /// unresolved, or no collision data.
        Method(
            isolate, proto, "getCollisionA", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (!d2bs::game::IsGameReady()) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;  // Returns undefined - reference returns undefined when room is null
                }

                auto flat = data->GetCollisionFlat();
                if (flat.empty()) {
                    return;  // Returns undefined - reference returns undefined when collision data is null
                }
                size_t byteLength = flat.size() * sizeof(uint16_t);
                auto backing = v8::ArrayBuffer::NewBackingStore(isolate, byteLength);
                std::memcpy(backing->Data(), flat.data(), byteLength);
                auto buffer = v8::ArrayBuffer::New(isolate, std::move(backing));
                args.GetReturnValue().Set(v8::Uint16Array::New(buffer, 0, flat.size()));
            });

        /// @description Returns Room objects adjacent to / near this room.
        /// @signature getNearby()
        /// @returns {Room[]} - nearby Room objects; empty array when unresolved or none.
        Method(
            isolate, proto, "getNearby", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto* data = Unwrap(args.This());
                if (!*data) {
                    args.GetReturnValue().Set(v8::Array::New(isolate, 0));
                    return;
                }

                auto nearby = data->GetNearby();
                auto array = v8::Array::New(isolate, static_cast<int32_t>(nearby.size()));

                for (uint32_t i = 0; i < nearby.size(); ++i) {
                    auto obj = CreateInstance(isolate, context, std::make_unique<d2bs::game::Room>(nearby[i]));
                    if (obj.IsEmpty()) {
                        v8_error::ThrowError(isolate, "Failed to build nearby room array");
                        return;
                    }
                    array->Set(context, i, obj).Check();
                }

                args.GetReturnValue().Set(array);
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
            isolate, proto, "getStat", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                args.GetReturnValue().SetNull();
                if (!d2bs::game::IsGameReady()) {
                    return;
                }
                if (args.Length() < 1 || !args[0]->IsNumber()) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;
                }
                int32_t nStat = v8_convert::ToInt32(isolate, args[0]);
                auto lock = d2bs::game::Bridge::Lock();
                args.GetReturnValue().Set(data->GetStat(nStat));
            });

        /// @description Returns a new Room object for the first room in this room's level (does not mutate this
        /// object, unlike getNext).
        /// @signature getFirst()
        /// @returns {Room} - the first Room in the level; undefined when unresolved or none.
        Method(
            isolate, proto, "getFirst", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;
                }
                auto first = data->GetFirst();
                if (!first) {
                    return;
                }
                auto obj = CreateInstance(isolate, context, std::make_unique<d2bs::game::Room>(first));
                if (obj.IsEmpty())
                    return;
                args.GetReturnValue().Set(obj);
            });

        /// @description Tests whether the given Unit is currently in this room.
        /// @signature unitInRoom(unit: Unit)
        /// @param unit {Unit} - the Unit object to test.
        /// @returns {boolean} - true if the unit is in this room, false otherwise; undefined on invalid/missing
        /// argument or unresolved handles.
        Method(
            isolate, proto, "unitInRoom", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                if (args.Length() < 1 || !args[0]->IsObject()) {
                    return;  // Returns undefined, matching reference early-return
                }
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;  // undefined, matching reference
                }

                // Extract game::Unit* from the passed Unit JS object
                auto unitObj = args[0].As<v8::Object>();
                if (!JSUnit::IsInstance(unitObj)) {
                    return;  // undefined, matching reference
                }
                auto* unitData = JSUnit::Unwrap(unitObj);
                if (!unitData || !*unitData) {
                    return;  // undefined, matching reference
                }

                args.GetReturnValue().Set(unitData->GetRoom() == *data);
            });
    }
};

}  // namespace d2bs::api::classes
