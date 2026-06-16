#include "GameFunctions.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <span>

#include <Windows.h>

#include "api/classes/game/JSArea.h"
#include "api/classes/game/JSControl.h"
#include "api/classes/game/JSParty.h"
#include "api/classes/game/JSPresetUnit.h"
#include "api/classes/game/JSRoom.h"
#include "api/classes/game/JSUnit.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/core/V8Extract.h"
#include "api/core/V8Function.h"
#include "api/globals/TxtLookup.h"
#include "components/config/AppConfig.h"
#include "components/pathfinding/Pathfinder.h"
#include "components/script/ScriptEngine.h"
#include "game/Bridge.h"
#include "game/Constants.h"
#include "game/Control.h"
#include "game/Finders.h"
#include "game/GameHelpers.h"
#include "game/Level.h"
#include "game/Party.h"
#include "game/Room.h"
#include "game/Unit.h"
#include "utils/utils.h"

namespace d2bs::api::globals {

using namespace d2bs::api;
using namespace d2bs::api::classes;

namespace {

// Emit a JS array of {x, y} objects from a span of pathfinding positions.
v8::Local<v8::Array> PositionsToV8(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                   std::span<const d2bs::pathfinding::Position> points) {
    auto arr = v8::Array::New(isolate, static_cast<int32_t>(points.size()));
    for (size_t i = 0; i < points.size(); ++i) {
        v8::HandleScope innerScope(isolate);
        arr->Set(context, static_cast<uint32_t>(i), v8_convert::ToV8(isolate, points[i])).Check();
    }
    return arr;
}

// Euclidean distance between two points (used by getDistance in all its arg shapes).
// Promoted out of the inline lambda so the algorithm lives in one place.
double Distance(d2bs::game::Point a, d2bs::game::Point b) {
    double dx = static_cast<double>(b.x - a.x);
    double dy = static_cast<double>(b.y - a.y);
    return std::abs(std::sqrt((dx * dx) + (dy * dy)));
}

}  // namespace

// NOLINTNEXTLINE(readability-function-size) - registration function, intentionally large
void RegisterGameFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global) {
    /// @description Find the first game unit matching optional type/name/classId/mode/id criteria.
    /// @signature getUnit(special: number)
    /// @param special {number} - 100 = cursor item, 101 = selected unit (falls back to selected inventory item)
    /// @signature getUnit(type: number, name?: string, mode?: number, unitId?: number)
    /// @param type {number} - unit type (0 = player, 1 = monster, 2 = object, 3 = missile, 4 = item, 5 = tile);
    /// out-of-range searches all types. Pass -1 (or omit) for no filter.
    /// @param name {string} - unit name filter. Pass -1 (or omit) for no filter.
    /// @param mode {number} - unit mode filter. Pass -1 (or omit) for no filter. Two special forms: if mode >= 100 and
    ///   the unit is an item, it filters by item location matching (mode - 100) - 100=ground, 101=equipped, 102=belt,
    ///   103=inventory, 104=store, 105=trade, 106=cube, 107=stash. If bit 29 is set (mode | 0x20000000), the low 28
    ///   bits are a bitmask: the unit matches when its mode equals any bit position 0..27 that is set in mode (e.g.
    ///   0x20000003 matches modes 0 or 1).
    /// @param unitId {number} - specific unit id filter. Pass -1 (or omit) for no filter.
    /// @signature getUnit(type: number, classId?: number, mode?: number, unitId?: number)
    /// @param classId {number} - class id filter (mutually exclusive with name). Pass -1 (or omit) for no filter.
    /// @returns {Unit|undefined} - matching unit, or undefined if not found
    v8_function::Register(
        isolate, global, "getUnit", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            // Reference: if (argc < 1) return JS_TRUE; (returns undefined with no args)
            if (args.Length() < 1) {
                return;
            }

            // Special types handled via arg[0] only: ITEM_LOCATION_MODE_OFFSET (100) =
            // cursor item, 101 = selected unit. Remaining args are ignored on these paths
            // (matches reference).
            if (args[0]->IsNumber()) {
                uint32_t type = v8_convert::ToUint32(isolate, args[0]);
                if (type == d2bs::game::ITEM_LOCATION_MODE_OFFSET) {
                    auto unit = d2bs::game::Unit::CursorItem();
                    if (!unit) {
                        return;
                    }
                    args.GetReturnValue().Set(
                        JSUnit::CreateInstance(isolate, context, std::make_unique<d2bs::game::Unit>(*unit)));
                    return;
                }
                if (type == d2bs::game::ITEM_LOCATION_MODE_OFFSET + 1) {
                    auto unit = d2bs::game::Unit::Selected();
                    // Reference lines 605-608: fall back to SelectedInventoryItem if no selected unit
                    if (!unit) {
                        unit = d2bs::game::Unit::SelectedInventoryItem();
                    }
                    if (!unit) {
                        return;
                    }
                    args.GetReturnValue().Set(
                        JSUnit::CreateInstance(isolate, context, std::make_unique<d2bs::game::Unit>(*unit)));
                    return;
                }
            }

            // Normal unit lookup: getUnit(type[, name/classId[, mode[, unitId]]])
            // Parsed linearly in arg order. IsUint32 (not IsNumber): scripts pass -1 to
            // mean "no filter" - IsUint32 rejects negative values, leaving optionals
            // nullopt. IsNumber would accept -1 and store 0xFFFFFFFF as engaged.
            d2bs::game::UnitCursorState cursor;
            if (args[0]->IsUint32()) {
                auto rawType = v8_convert::ToUint32(isolate, args[0]);
                // Valid unit types are 0-5. Values outside this range trigger "search all types."
                if (rawType <= static_cast<uint32_t>(d2bs::game::UnitType::Tile)) {
                    cursor.type = static_cast<d2bs::game::UnitType>(rawType);
                }
            }
            if (args.Length() >= 2) {
                // arg[1] is string (name) OR uint32 (classId) - mutually exclusive.
                if (args[1]->IsString()) {
                    cursor.name = v8_convert::ToString(isolate, args[1]);
                } else if (args[1]->IsUint32()) {
                    cursor.classId = v8_convert::ToUint32(isolate, args[1]);
                }
            }
            if (args.Length() >= 3 && args[2]->IsUint32()) {
                cursor.mode = v8_convert::ToUint32(isolate, args[2]);
            }
            if (args.Length() >= 4 && args[3]->IsUint32()) {
                cursor.unitId = v8_convert::ToUint32(isolate, args[3]);
            }

            // Reference calls GetUnit(szName, nClassId, nType, nMode, nUnitId) which searches
            // by all-optional cursor criteria; FindFirst stamps the cursor onto the result.
            auto unit = d2bs::game::Unit::FindFirst(cursor);
            if (!unit) {
                return;
            }

            // Reference line 624: getUnit always uses PRIVATE_UNIT
            // Reference line 625: store search params, not live unit values (FindFirst already stamps).
            args.GetReturnValue().Set(
                JSUnit::CreateInstance(isolate, context, std::make_unique<d2bs::game::Unit>(*unit)));
        });

    /// @description Compute an A* path between two world coordinates on a level.
    /// @signature getPath(area: number, srcX: number, srcY: number, dstX: number, dstY: number, reductionType?: number,
    /// radius?: number, reject?: function, reduce?: function, mutate?: function)
    /// @param area {number} - level/area id (must be non-zero)
    /// @param srcX {number} - source x world coordinate
    /// @param srcY {number} - source y world coordinate
    /// @param dstX {number} - destination x world coordinate
    /// @param dstY {number} - destination y world coordinate
    /// @param reductionType {number} - path reduction mode (default 0): 0 = walk, 1 = teleport, 2 = none, 3 = callback
    /// (requires reject/reduce/mutate)
    /// @param radius {number} - search radius (default 20)
    /// @param reject {function} - rejects a point; required when reductionType == 3
    /// @param reduce {function} - reduces the point list; required when reductionType == 3
    /// @param mutate {function} - mutates a point; required when reductionType == 3
    /// @callback reject(x: number, y: number) -> {boolean} - return true to drop this point from the path
    /// @callback reduce(path: Array<{x: number, y: number}>) -> {Array<{x: number, y: number}>} - return the reduced
    /// point list
    /// @callback mutate(x: number, y: number) -> {[number, number]} - return the mutated point as a positional [x, y]
    /// array
    /// @returns {Array<{x:number,y:number}>} - array of path positions (possibly empty)
    /// @throws {Error} - area is 0 (invalid level)
    /// @throws {RangeError} - reductionType is outside 0-3
    /// @throws {Error} - reductionType is 3 but reject/reduce/mutate functions are missing
    v8_function::Register(
        isolate, global, "getPath", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Delegates to d2bs::pathfinding::FindPath after parsing and validating args.
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 5) {
                v8_error::ThrowTypeError(isolate, "getPath requires at least 5 arguments");
                return;
            }

            uint32_t area = v8_convert::ToUint32(isolate, args[0]);
            auto src = v8_extract::Position(args, 1).value_or(d2bs::game::Position::Zero);
            auto dst = v8_extract::Position(args, 3).value_or(d2bs::game::Position::Zero);
            uint32_t reductionType = 0;
            uint32_t radius = 20;
            if (args.Length() > 5) {
                reductionType = v8_convert::ToUint32(isolate, args[5]);
            }
            if (args.Length() > 6) {
                radius = v8_convert::ToUint32(isolate, args[6]);
            }

            if (area == 0) {
                v8_error::ThrowError(isolate, "Invalid level passed to getPath");
                return;
            }

            if (reductionType > static_cast<uint32_t>(d2bs::pathfinding::ReductionType::JSCallback)) {
                v8_error::ThrowRangeError(isolate, "reductionType must be 0-3");
                return;
            }

            if (reductionType == static_cast<uint32_t>(d2bs::pathfinding::ReductionType::JSCallback) &&
                (args.Length() < 10 || !args[7]->IsFunction() || !args[8]->IsFunction() || !args[9]->IsFunction())) {
                v8_error::ThrowError(isolate, "Invalid function values for reduction type");
                return;
            }

            auto context = isolate->GetCurrentContext();

            d2bs::pathfinding::PathRequest request;
            request.areaId = area;
            request.start = src;
            request.end = dst;
            request.reduction = static_cast<d2bs::pathfinding::ReductionType>(reductionType);
            request.radius = static_cast<int32_t>(radius);
            if (auto* script = d2bs::ScriptEngine::Instance().GetScript(isolate)) {
                request.cancelToken = script->GetStopToken();
            }

            if (reductionType == static_cast<uint32_t>(d2bs::pathfinding::ReductionType::JSCallback)) {
                auto rejectFunc = args[7].As<v8::Function>();
                auto reduceFunc = args[8].As<v8::Function>();
                auto mutateFunc = args[9].As<v8::Function>();

                request.jsReject = [isolate, context, rejectFunc](d2bs::pathfinding::Position p) -> bool {
                    v8::HandleScope scope(isolate);
                    v8::TryCatch tryCatch(isolate);
                    std::array<v8::Local<v8::Value>, 2> argv = {v8_convert::ToV8(isolate, p.x),
                                                                v8_convert::ToV8(isolate, p.y)};
                    auto result = rejectFunc->Call(context, v8::Undefined(isolate), argv.size(), argv.data());
                    if (tryCatch.HasCaught() || result.IsEmpty())
                        return false;
                    return result.ToLocalChecked()->BooleanValue(isolate);
                };

                request.jsReduce = [isolate, context, reduceFunc](const std::vector<d2bs::pathfinding::Position>& path)
                    -> std::vector<d2bs::pathfinding::Position> {
                    v8::HandleScope scope(isolate);
                    v8::TryCatch tryCatch(isolate);

                    auto pathArr = PositionsToV8(isolate, context, path);

                    std::array<v8::Local<v8::Value>, 1> argv = {pathArr};
                    auto callResult = reduceFunc->Call(context, v8::Undefined(isolate), argv.size(), argv.data());
                    if (tryCatch.HasCaught() || callResult.IsEmpty() || !callResult.ToLocalChecked()->IsArray()) {
                        return path;
                    }

                    auto resultArr = callResult.ToLocalChecked().As<v8::Array>();
                    std::vector<d2bs::pathfinding::Position> reduced;
                    reduced.reserve(resultArr->Length());
                    for (uint32_t i = 0; i < resultArr->Length(); i++) {
                        auto elem = resultArr->Get(context, i).ToLocalChecked();
                        if (auto pt = v8_extract::Position(isolate, elem))
                            reduced.push_back(*pt);
                    }
                    return reduced;
                };

                request.jsMutate = [isolate, context,
                                    mutateFunc](d2bs::pathfinding::Position p) -> d2bs::pathfinding::Position {
                    v8::HandleScope scope(isolate);
                    v8::TryCatch tryCatch(isolate);
                    std::array<v8::Local<v8::Value>, 2> argv = {v8_convert::ToV8(isolate, p.x),
                                                                v8_convert::ToV8(isolate, p.y)};
                    auto result = mutateFunc->Call(context, v8::Undefined(isolate), argv.size(), argv.data());
                    if (tryCatch.HasCaught() || result.IsEmpty() || !result.ToLocalChecked()->IsArray())
                        return p;
                    auto arr = result.ToLocalChecked().As<v8::Array>();
                    auto rxMaybe = arr->Get(context, 0);
                    auto ryMaybe = arr->Get(context, 1);
                    if (rxMaybe.IsEmpty() || ryMaybe.IsEmpty())
                        return p;
                    auto rx = v8_convert::ToUint32(isolate, rxMaybe.ToLocalChecked());
                    auto ry = v8_convert::ToUint32(isolate, ryMaybe.ToLocalChecked());
                    return {.x = rx, .y = ry};
                };
            }

            auto path = d2bs::pathfinding::FindPath(request);
            args.GetReturnValue().Set(PositionsToV8(isolate, context, path));
        });

    /// @description Read the collision flag at a world coordinate on a level.
    /// @signature getCollision(levelId: number, x: number, y: number)
    /// @param levelId {number} - level id
    /// @param x {number} - world x coordinate
    /// @param y {number} - world y coordinate
    /// @returns {number} - collision flag at the cell, or 0 if no room contains the position
    /// @throws {Error} - the level is not loaded
    v8_function::Register(
        isolate, global, "getCollision", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 3 || !args[0]->IsUint32() || !args[1]->IsUint32() || !args[2]->IsUint32()) {
                v8_error::ThrowTypeError(isolate, "Invalid arguments.");
                return;
            }

            uint32_t levelId = v8_convert::ToUint32(isolate, args[0]);
            auto pos = v8_extract::Position(args, 1).value();  // strict IsUint32 above guarantees this

            auto lock = d2bs::game::Bridge::Lock();

            auto level = d2bs::game::Level::Get(levelId);
            if (!level) {
                v8_error::ThrowError(isolate, "Level not loaded");
                return;
            }

            if (auto room = level->FindRoomAt(pos)) {
                auto rb = room->Bounds();
                auto local = pos - rb.origin;
                auto collision = room->GetCollisionFlat();
                size_t idx = (local.y * rb.size.width) + local.x;
                if (idx < collision.size()) {
                    args.GetReturnValue().Set(collision[idx]);
                    return;
                }
            }

            args.GetReturnValue().Set(0);
        });

    /// @description Get the player's mercenary HP as a percentage (0-100).
    /// @signature getMercHP()
    /// @returns {number} - Merc HP as a percent (0-100); undefined if there is no player or merc.
    v8_function::Register(
        isolate, global, "getMercHP", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            // Reference: global version always uses player unit
            auto player = d2bs::game::Unit::Player();
            if (!player)
                return;
            if (player.Mode() == 12) {
                args.GetReturnValue().Set(0);
                return;
            }
            auto merc = player.FindMerc();
            if (!merc)
                return;
            uint32_t maxHp = merc->HpMax();
            args.GetReturnValue().Set(maxHp > 0 ? (100 * merc->Hp()) / maxHp : 0);
        });

    /// @description Get the current cursor type.
    /// @signature getCursorType(mode?: number)
    /// @param mode {number} - 1 = shop cursor, otherwise regular cursor (default 0)
    /// @returns {number} - cursor type id
    v8_function::Register(
        isolate, global, "getCursorType", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            int32_t type = 0;
            if (args.Length() > 0) {
                type = v8_convert::ToInt32(isolate, args[0]);
            }

            // type 1 = shop mode (check for shop-specific cursor), 0 = regular cursor
            bool isShop = (type == 1);
            args.GetReturnValue().Set(static_cast<int32_t>(d2bs::game::GetCursorType(isShop)));
        });

    /// @description Look up a skill id by its localized name.
    /// @signature getSkillByName(name: string)
    /// @param name {string} - skill name
    /// @returns {number|undefined} - skill id, or undefined if not found
    v8_function::Register(
        isolate, global, "getSkillByName", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsString()) {
                return;
            }

            std::string name = v8_convert::ToString(isolate, args[0]);
            auto skillId = d2bs::game::GetSkillByName(name);
            if (!skillId) {
                return;
            }
            args.GetReturnValue().Set(*skillId);
        });

    /// @description Resolve a skill's localized name from its id (skills.txt -> skilldesc -> locale string).
    /// @signature getSkillById(skillId: number)
    /// @param skillId {number} - skill id
    /// @returns {string} - localized skill name, or "Unknown" on lookup failure
    v8_function::Register(
        isolate, global, "getSkillById", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, "Unknown"));
                return;
            }

            int32_t skillId = v8_convert::ToInt32(isolate, args[0]);

            // Chain: skills table -> skilldesc row -> locale string ID -> localized name
            static_assert(std::variant_size_v<d2bs::game::TxtValue> == 3,
                          "TxtValue alternatives changed - review the get_if<int64_t> assumption below");
            auto descCell = game::GetTxtValue("skills", skillId, "skilldesc");
            if (auto* descRow = std::get_if<int64_t>(&descCell); descRow != nullptr && *descRow > 0) {
                auto strCell = game::GetTxtValue("skilldesc", static_cast<uint32_t>(*descRow), "str name");
                if (auto* strId = std::get_if<int64_t>(&strCell); strId != nullptr && *strId > 0) {
                    auto name = game::GetLocaleString(static_cast<uint16_t>(*strId));
                    if (!name.empty()) {
                        args.GetReturnValue().Set(v8_convert::ToV8(isolate, name));
                        return;
                    }
                }
            }
            // Fallback: return "Unknown" (matching reference behavior)
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, "Unknown"));
        });

    /// @description Get a localized game string by its numeric locale string id.
    /// @signature getLocaleString(localeId: number)
    /// @param localeId {number} - locale string id (lower 16 bits used)
    /// @returns {string|undefined} - the localized string (may be empty), or undefined if missing arg
    v8_function::Register(
        isolate, global, "getLocaleString", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetUndefined();
                return;
            }

            uint16_t localeId = static_cast<uint16_t>(v8_convert::ToUint32(isolate, args[0]));
            std::string text = d2bs::game::GetLocaleString(localeId);
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, text));
        });

    /// @description Measure the pixel size of a text string in a given game font.
    /// @signature getTextSize(text: string, font: number, asObject?: boolean)
    /// @param text {string} - the text to measure
    /// @param font {number} - game font id
    /// @param asObject {boolean} - true returns {width, height}, false/omitted returns [width, height]
    /// @returns {Array<number>|{width:number,height:number}|false} - size as array or object, false on bad arg types
    v8_function::Register(
        isolate, global, "getTextSize", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (args.Length() < 2) {
                v8_error::ThrowTypeError(isolate, "getTextSize requires at least 2 arguments");
                return;
            }

            if (!args[0]->IsString() || !args[1]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            std::string text = v8_convert::ToString(isolate, args[0]);
            uint32_t font = v8_convert::ToUint32(isolate, args[1]);

            bool asObject = false;
            if (args.Length() > 2 && args[2]->IsBoolean()) {
                asObject = args[2]->BooleanValue(isolate);
            }

            auto size = d2bs::game::GetTextSize(text, font);

            if (asObject) {
                auto obj = v8::Object::New(isolate);
                obj->Set(context, v8_convert::ToV8(isolate, "width"),
                         v8_convert::ToV8(isolate, static_cast<int32_t>(size.width)))
                    .Check();
                obj->Set(context, v8_convert::ToV8(isolate, "height"),
                         v8_convert::ToV8(isolate, static_cast<int32_t>(size.height)))
                    .Check();
                args.GetReturnValue().Set(obj);
            } else {
                auto arr = v8::Array::New(isolate, 2);
                arr->Set(context, 0, v8_convert::ToV8(isolate, static_cast<int32_t>(size.width))).Check();
                arr->Set(context, 1, v8_convert::ToV8(isolate, static_cast<int32_t>(size.height))).Check();
                args.GetReturnValue().Set(arr);
            }
        });

    /// @description Query whether a UI flag is set.
    /// @signature getUIFlag(flag: number)
    /// @param flag {number} - UI flag id
    /// @returns {boolean|undefined} - true if the flag is set, undefined if missing arg
    v8_function::Register(
        isolate, global, "getUIFlag", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                return;
            }

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            uint32_t flag = v8_convert::ToUint32(isolate, args[0]);
            args.GetReturnValue().Set(d2bs::game::GetUIFlag(flag) != 0);
        });

    /// @description Query trade-related info by mode.
    /// @signature getTradeInfo(mode: number)
    /// @param mode {number} - 0 = recent trade id, 1 = recent trade name, 2 = recent trade id (alt)
    /// @returns {number|string|null|false} - id for modes 0/2, name-or-null for mode 1, false otherwise
    v8_function::Register(
        isolate, global, "getTradeInfo", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetFalse();

            if (args.Length() < 1) {
                return;
            }

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            // Reference line 948: must be an integer argument
            if (!args[0]->IsNumber()) {
                return;
            }

            auto mode = static_cast<d2bs::game::TradeInfoMode>(v8_convert::ToInt32(isolate, args[0]));
            switch (mode) {
                case d2bs::game::TradeInfoMode::RecentTradeId:
                    // Reference: INT_TO_JSVAL(*p_D2CLIENT_RecentTradeId)
                    args.GetReturnValue().Set(d2bs::game::GetRecentTradeId());
                    break;
                case d2bs::game::TradeInfoMode::RecentTradeName: {
                    // Reference: returns name string or null
                    auto info = d2bs::game::GetTradeInfo(d2bs::game::TradeInfoMode::RecentTradeName);
                    if (info) {
                        args.GetReturnValue().Set(v8_convert::ToV8(isolate, *info));
                    } else {
                        args.GetReturnValue().SetNull();
                    }
                    break;
                }
                case d2bs::game::TradeInfoMode::RecentTradeId2:
                    // Reference: INT_TO_JSVAL(*p_D2CLIENT_RecentTradeId)
                    args.GetReturnValue().Set(d2bs::game::GetRecentTradeId());
                    break;
                default:
                    break;
            }
        });

    /// @description Check whether a waypoint is active/known.
    /// @signature getWaypoint(waypointId: number)
    /// @param waypointId {number} - waypoint id (values > 40 are clamped to 0)
    /// @returns {boolean} - true if the waypoint is set
    v8_function::Register(
        isolate, global, "getWaypoint", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            uint32_t waypointId = v8_convert::ToUint32(isolate, args[0]);
            // Clamp waypointId > MAX_WAYPOINT_ID to 0 (matching reference behavior)
            constexpr uint32_t MAX_WAYPOINT_ID = 40;
            if (waypointId > MAX_WAYPOINT_ID) {
                waypointId = 0;
            }
            args.GetReturnValue().Set(d2bs::game::HasWaypoint(waypointId) != 0);
        });

    /// @description Get a Room object by level, by coordinates, or for the player's current level.
    /// @signature getRoom()
    /// @signature getRoom(levelId: number)
    /// @param levelId {number} - level id; 0 returns the player's current room, else the level's first room
    /// @signature getRoom(x: number, y: number)
    /// @param x {number} - world x coordinate (must be non-zero); searches the player's level
    /// @param y {number} - world y coordinate (must be non-zero)
    /// @signature getRoom(levelId: number, x: number, y: number)
    /// @returns {Room|undefined} - the matching Room; falls back to the level's first room if no coordinate match
    /// @throws {Error} - the game is not ready
    v8_function::Register(
        isolate, global, "getRoom", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::ThrowError(isolate, "Game not ready");
                return;
            }

            auto lock = d2bs::game::Bridge::Lock();
            d2bs::game::Room room;

            if (args.Length() == 0) {
                // No args: return the level's first room2 (pRoom2First), not the player's current room2.
                // Reference: D2CLIENT_GetPlayerUnit()->pPath->pRoom1->pRoom2->pLevel->pRoom2First
                auto player = d2bs::game::Unit::Player();
                if (!player) {
                    return;
                }
                auto playerRoom = player.GetRoom();
                if (!playerRoom) {
                    return;
                }
                auto level = playerRoom.GetLevel();
                if (!level) {
                    return;
                }
                room = level.GetFirstRoom();
            } else if (args.Length() == 1 && args[0]->IsNumber()) {
                // getRoom(levelId): get first room of the level, or player's room if 0
                uint32_t levelId = v8_convert::ToUint32(isolate, args[0]);
                if (levelId == 0) {
                    // Reference: levelId==0 returns player's current room
                    auto player = d2bs::game::Unit::Player();
                    if (!player) {
                        return;
                    }
                    room = player.GetRoom();
                } else {
                    auto level = d2bs::game::Level::Get(levelId);
                    if (!level) {
                        return;
                    }
                    room = level->GetFirstRoom();
                }
            } else if (args.Length() >= 2) {
                // getRoom(x, y) or getRoom(levelId, x, y): find room at coordinates
                auto pos = d2bs::game::Position::Zero;
                std::optional<d2bs::game::Level> level;

                if (args.Length() >= 3 && args[0]->IsNumber() && args[1]->IsNumber() && args[2]->IsNumber()) {
                    // getRoom(levelId, x, y)
                    uint32_t levelId = v8_convert::ToUint32(isolate, args[0]);
                    pos = v8_extract::Position(args, 1).value_or(d2bs::game::Position::Zero);
                    level = d2bs::game::Level::Get(levelId);
                    if (!level) {
                        return;
                    }
                } else {
                    // getRoom(x, y): search from player's room
                    pos = v8_extract::Position(args, 0).value_or(d2bs::game::Position::Zero);
                    auto player = d2bs::game::Unit::Player();
                    if (!player) {
                        return;
                    }
                    auto playerRoom = player.GetRoom();
                    if (!playerRoom) {
                        return;
                    }
                    auto pRoomLevel = playerRoom.GetLevel();
                    if (!pRoomLevel) {
                        return;
                    }
                    level = pRoomLevel;
                }

                // Reference line 530: zero coordinates return undefined
                if (pos.x == 0 || pos.y == 0)
                    return;

                if (auto match = level->FindRoomAt(pos)) {
                    room = *match;
                } else {
                    // Reference lines 561-567: if no room matched coordinates, fall back to level's first room
                    room = level->GetFirstRoom();
                }
            }

            if (!room) {
                return;
            }

            auto obj = JSRoom::CreateInstance(isolate, context, std::make_unique<d2bs::game::Room>(room));
            if (obj.IsEmpty())
                return;
            args.GetReturnValue().Set(obj);
        });

    /// @description Get a Party (roster) member, the first member, or one matched by name / id / unit.
    /// @signature getParty()
    /// @signature getParty(name: string)
    /// @param name {string} - player name to search for
    /// @signature getParty(unitId: number)
    /// @param unitId {number} - unit id to search for
    /// @signature getParty(unit: Unit)
    /// @param unit {Unit} - a Unit object; matched by its unit id
    /// @returns {Party|undefined} - the matching Party member, or undefined if not found
    v8_function::Register(
        isolate, global, "getParty", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto lock = d2bs::game::Bridge::Lock();

            // No args: return first party member
            if (args.Length() == 0) {
                auto firstOpt = d2bs::game::Party::GetFirst();
                if (!firstOpt) {
                    return;
                }
                auto obj = JSParty::CreateInstance(isolate, context, std::make_unique<d2bs::game::Party>(*firstOpt));
                if (obj.IsEmpty())
                    return;
                args.GetReturnValue().Set(obj);
                return;
            }

            // Search by name (string arg) or unit ID (number arg)
            std::optional<d2bs::game::Party> found;
            if (args[0]->IsString()) {
                std::string name = v8_convert::ToString(isolate, args[0]);
                found = d2bs::game::Party::FindByName(name);
            } else if (args[0]->IsNumber()) {
                uint32_t unitId = v8_convert::ToUint32(isolate, args[0]);
                found = d2bs::game::Party::FindById(unitId);
            } else if (args[0]->IsObject()) {
                // If a unit object is passed, match by its unit ID
                auto unitObj = args[0].As<v8::Object>();
                if (JSUnit::IsInstance(unitObj)) {
                    auto* unitData = JSUnit::Unwrap(unitObj);
                    if (unitData && *unitData) {
                        found = d2bs::game::Party::FindById(unitData->Id());
                    } else {
                        v8_error::ThrowError(isolate, "Unable to get Unit");
                        return;
                    }
                }
            }

            if (!found) {
                return;
            }
            auto obj = JSParty::CreateInstance(isolate, context, std::make_unique<d2bs::game::Party>(*found));
            if (obj.IsEmpty())
                return;
            args.GetReturnValue().Set(obj);
        });

    /// @description Find the first preset unit in a level, optionally filtered by type and class id.
    /// @signature getPresetUnit(areaId: number, type?: number, classId?: number)
    /// @param areaId {number} - level/area id
    /// @param type {number} - unit type to match; pass -1 or omit to match any type.
    /// @param classId {number} - unit class id to match; pass -1 or omit to match any class.
    /// @returns {PresetUnit|false} - the first matching preset unit, or false if none
    /// @throws {Error} - the level cannot be accessed
    v8_function::Register(
        isolate, global, "getPresetUnit", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto lock = d2bs::game::Bridge::Lock();

            if (args.Length() < 1) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint32_t areaId = v8_convert::ToUint32(isolate, args[0]);
            std::optional<uint32_t> nType;
            std::optional<uint32_t> nClassId;
            if (args.Length() > 1 && args[1]->IsUint32()) {
                nType = v8_convert::ToUint32(isolate, args[1]);
            }
            if (args.Length() > 2 && args[2]->IsUint32()) {
                nClassId = v8_convert::ToUint32(isolate, args[2]);
            }

            auto level = d2bs::game::Level::Get(areaId);
            if (!level) {
                v8_error::ThrowError(isolate, "getPresetUnit failed, couldn't access the level!");
                return;
            }

            auto matches = level->GetPresetUnits(nType, nClassId);
            if (matches.empty()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto data = std::make_unique<d2bs::game::PresetUnitInfo>(matches.front());
            auto obj = JSPresetUnit::CreateInstance(isolate, context, std::move(data));
            if (obj.IsEmpty()) {
                v8_error::ThrowError(isolate, "Failed to create PresetUnit object");
                return;
            }
            args.GetReturnValue().Set(obj);
        });

    /// @description Get all preset units in a level as an array, optionally filtered by type and class id.
    /// @signature getPresetUnits(areaId: number, type?: number, classId?: number)
    /// @param areaId {number} - level/area id
    /// @param type {number} - unit type to match; pass -1 or omit to match any type.
    /// @param classId {number} - unit class id to match; pass -1 or omit to match any class.
    /// @returns {Array<PresetUnit>|false} - array of matching preset units (possibly empty), or false if no args
    /// @throws {Error} - the level cannot be accessed
    v8_function::Register(
        isolate, global, "getPresetUnits", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto lock = d2bs::game::Bridge::Lock();

            if (args.Length() < 1) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint32_t areaId = v8_convert::ToUint32(isolate, args[0]);
            std::optional<uint32_t> nType;
            std::optional<uint32_t> nClassId;
            if (args.Length() > 1 && args[1]->IsUint32()) {
                nType = v8_convert::ToUint32(isolate, args[1]);
            }
            if (args.Length() > 2 && args[2]->IsUint32()) {
                nClassId = v8_convert::ToUint32(isolate, args[2]);
            }

            auto level = d2bs::game::Level::Get(areaId);
            if (!level) {
                v8_error::ThrowError(isolate, "getPresetUnits failed, couldn't access the level!");
                return;
            }

            auto allPresets = level->GetPresetUnits(nType, nClassId);
            auto array = v8::Array::New(isolate, static_cast<int32_t>(allPresets.size()));
            for (size_t i = 0; i < allPresets.size(); ++i) {
                auto data = std::make_unique<d2bs::game::PresetUnitInfo>(allPresets[i]);
                auto obj = JSPresetUnit::CreateInstance(isolate, context, std::move(data));
                if (obj.IsEmpty()) {
                    v8_error::ThrowError(isolate, "Failed to build preset unit object");
                    return;
                }
                array->Set(context, static_cast<uint32_t>(i), obj).Check();
            }

            args.GetReturnValue().Set(array);
        });

    /// @description Get an Area object for a level, defaulting to the player's current area.
    /// @signature getArea(areaId?: number)
    /// @param areaId {number} - area/level id (non-negative); omitted uses the player's current area
    /// @returns {Area|false|undefined} - the Area object, false if the level is not loaded, undefined if no player
    /// @throws {Error} - the game is not ready
    /// @throws {Error} - areaId is negative
    v8_function::Register(
        isolate, global, "getArea", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::ThrowError(isolate, "Game not ready");
                return;
            }

            uint32_t areaId = 0;
            if (args.Length() >= 1) {
                if (!args[0]->IsNumber()) {
                    v8_error::ThrowError(isolate, "Invalid parameter passed to getArea!");
                    return;
                }
                int32_t signedId = v8_convert::ToInt32(isolate, args[0]);
                if (signedId < 0) {
                    v8_error::ThrowError(isolate, "Invalid parameter passed to getArea!");
                    return;
                }
                areaId = static_cast<uint32_t>(signedId);
            } else {
                // Default: use player's current area
                auto player = d2bs::game::Unit::Player();
                if (!player) {
                    return;
                }
                areaId = player.Area();
            }

            auto level = d2bs::game::Level::Get(areaId);
            if (!level) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto obj = JSArea::CreateInstance(isolate, context, std::make_unique<d2bs::game::Level>(areaId));
            if (obj.IsEmpty())
                return;
            args.GetReturnValue().Set(obj);
        });

    /// @description Read a value from a game data (.txt) table by table, row, and column.
    /// @signature getBaseStat(table: string, row: number, column: string)
    /// @signature getBaseStat(table: number, row: number, column: number)
    /// @param table {string|number} - table name, or index into the known table-name list
    /// @param row {number} - row index
    /// @param column {string|number} - column name, or index into the resolved table's columns
    /// @returns {number|string|undefined} - the cell value, or undefined if unresolved or the cell is empty
    v8_function::Register(
        isolate, global, "getBaseStat", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 3) {
                return;
            }

            // Table arg: string (canonical name) or number (index into TXT_TABLE_NAMES).
            std::string tableName;
            if (args[0]->IsString()) {
                tableName = v8_convert::ToString(isolate, args[0]);
            } else if (args[0]->IsNumber()) {
                auto resolved = ResolveTxtTable(v8_convert::ToUint32(isolate, args[0]));
                if (!resolved) {
                    return;
                }
                tableName = std::string(*resolved);
            } else {
                return;
            }

            uint32_t row = v8_convert::ToUint32(isolate, args[1]);

            // Column arg: string (canonical name) or number (index into the table's columns).
            std::string columnName;
            if (args[2]->IsString()) {
                columnName = v8_convert::ToString(isolate, args[2]);
            } else if (args[2]->IsNumber()) {
                auto resolved = ResolveTxtColumn(tableName, v8_convert::ToUint32(isolate, args[2]));
                if (!resolved) {
                    return;
                }
                columnName = std::string(*resolved);
            } else {
                return;
            }

            auto value = d2bs::game::GetTxtValue(tableName, row, columnName);
            // Guardrail: if a new alternative is added to TxtValue, this get_if chain must be
            // extended to avoid silently dropping values. Bump the count + add a branch below.
            static_assert(std::variant_size_v<d2bs::game::TxtValue> == 3,
                          "TxtValue alternatives changed - update the get_if chain below");
            if (auto* n = std::get_if<int64_t>(&value)) {
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(*n)));
            } else if (auto* s = std::get_if<std::string>(&value)) {
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, *s));
            } else {
                args.GetReturnValue().SetUndefined();
            }
        });

    /// @description Find the first UI control matching optional position/size filters; menu state only.
    /// @signature getControl(type?: number, x?: number, y?: number, xsize?: number, ysize?: number)
    /// @param type {number} - control type filter
    /// @param x {number} - x position filter
    /// @param y {number} - y position filter
    /// @param xsize {number} - width filter
    /// @param ysize {number} - height filter
    /// @returns {Control|undefined} - the matching control, or undefined if not in menu state / no match
    v8_function::Register(
        isolate, global, "getControl", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Reference JSControl.cpp line 295: controls only exist in menu state
            if (d2bs::game::GetGameState() != d2bs::game::GameState::Menu) {
                return;
            }

            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            std::optional<d2bs::game::ControlType> type;
            std::optional<uint32_t> x;
            std::optional<uint32_t> y;
            std::optional<uint32_t> xsize;
            std::optional<uint32_t> ysize;

            if (args.Length() > 0 && args[0]->IsUint32()) {
                type = static_cast<d2bs::game::ControlType>(v8_convert::ToUint32(isolate, args[0]));
            }
            if (args.Length() > 1 && args[1]->IsUint32()) {
                x = v8_convert::ToUint32(isolate, args[1]);
            }
            if (args.Length() > 2 && args[2]->IsUint32()) {
                y = v8_convert::ToUint32(isolate, args[2]);
            }
            if (args.Length() > 3 && args[3]->IsUint32()) {
                xsize = v8_convert::ToUint32(isolate, args[3]);
            }
            if (args.Length() > 4 && args[4]->IsUint32()) {
                ysize = v8_convert::ToUint32(isolate, args[4]);
            }

            auto ctrl = d2bs::game::Control::Find(type, x, y, xsize, ysize);
            if (!ctrl) {
                return;
            }

            auto obj = JSControl::CreateInstance(isolate, context, std::make_unique<d2bs::game::Control>(*ctrl));
            if (obj.IsEmpty())
                return;
            args.GetReturnValue().Set(obj);
        });

    /// @description Get all current UI controls as an array; menu state only.
    /// @signature getControls()
    /// @returns {Array<Control>|undefined} - all controls (possibly empty), or undefined if not in menu state
    v8_function::Register(
        isolate, global, "getControls", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Reference JSControl.cpp line 328: controls only exist in menu state
            if (d2bs::game::GetGameState() != d2bs::game::GameState::Menu) {
                return;
            }

            auto* isolate = args.GetIsolate();

            auto array = v8::Array::New(isolate);

            if (auto firstCtrl = d2bs::game::Control::GetFirst()) {
                auto context = isolate->GetCurrentContext();
                uint32_t idx = 0;
                for (auto ctrl = *firstCtrl; ctrl; ctrl = ctrl.GetNext()) {
                    auto obj = JSControl::CreateInstance(isolate, context, std::make_unique<d2bs::game::Control>(ctrl));
                    if (obj.IsEmpty())
                        continue;
                    array->Set(context, idx++, obj).Check();
                }
            }

            args.GetReturnValue().Set(array);
        });

    /// @description Test a PvP/relationship flag between two units.
    /// @signature getPlayerFlag(unitId1: number, unitId2: number, flag: number)
    /// @param unitId1 {number} - first unit id
    /// @param unitId2 {number} - second unit id
    /// @param flag {number} - PvP flag mask to test
    /// @returns {boolean|undefined} - flag test result, false if a unit is unresolved, undefined on bad args
    v8_function::Register(
        isolate, global, "getPlayerFlag", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() != 3) {
                return;
            }

            if (!args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber()) {
                return;
            }

            uint32_t unitId1 = v8_convert::ToUint32(isolate, args[0]);
            uint32_t unitId2 = v8_convert::ToUint32(isolate, args[1]);
            uint32_t flag = v8_convert::ToUint32(isolate, args[2]);

            // Resolve both unit IDs to Unit handles. TestPvpFlag takes const Unit& and re-resolves
            // by id internally.
            auto u1 = d2bs::game::Unit::Find(unitId1);
            auto u2 = d2bs::game::Unit::Find(unitId2);
            if (!u1 || !u2) {
                args.GetReturnValue().Set(false);
                return;
            }
            args.GetReturnValue().Set(d2bs::game::TestPvpFlag(*u1, *u2, flag));
        });

    /// @description Get the NPC the player is currently interacting with.
    /// @signature getInteractedNPC()
    /// @returns {Unit|false} - the interacting NPC, or false if none
    v8_function::Register(
        isolate, global, "getInteractedNPC", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto npc = d2bs::game::Unit::InteractingNPC();
            if (!npc) {
                args.GetReturnValue().SetFalse();
                return;
            }

            args.GetReturnValue().Set(
                JSUnit::CreateInstance(isolate, context, std::make_unique<d2bs::game::Unit>(*npc)));
        });

    /// @description Check whether NPC dialog text is currently scrolling/displaying.
    /// @signature getIsTalkingNPC()
    /// @returns {boolean} - true if dialog text is scrolling
    v8_function::Register(
        isolate, global, "getIsTalkingNPC", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            // Check if NPC dialog text is currently scrolling (not just interacting)
            args.GetReturnValue().Set(d2bs::game::IsScrollingText());
        });

    /// @description Get the current NPC dialog menu lines; each line's handler() clicks it (matched by text).
    /// @signature getDialogLines()
    /// @returns {Array<{text:string,selectable:boolean,handler:function}>|undefined} - dialog lines, or undefined if
    /// none
    /// @throws {Error} - a line's handler() is invoked but that dialog line is no longer clickable
    v8_function::Register(
        isolate, global, "getDialogLines", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            auto lock = d2bs::game::Bridge::Lock();
            auto lines = d2bs::game::GetDialogLines();
            if (lines.empty()) {
                return;
            }

            // Reference: each line has .text, .selectable, and .handler (a callable function)
            auto array = v8::Array::New(isolate, static_cast<int32_t>(lines.size()));
            for (size_t i = 0; i < lines.size(); ++i) {
                v8::HandleScope innerScope(isolate);
                // Each line gets its own handler that captures the line index via the data parameter
                auto handlerTpl = v8::FunctionTemplate::New(
                    isolate,
                    +[](const v8::FunctionCallbackInfo<v8::Value>& handlerArgs) {
                        // The line text was baked into the FunctionTemplate's Data slot
                        // when the closure was constructed (see Data arg below). Matching
                        // by text rather than index makes the call robust to dialog
                        // churn - a different dialog opened in the meantime won't accept
                        // the click unless it has a line with the same text.
                        auto* iso = handlerArgs.GetIsolate();
                        const std::string text = v8_convert::ToString(iso, handlerArgs.Data());
                        if (!d2bs::game::SelectDialogLineByText(text)) {
                            // Reference parallel: my_clickDialog at JSGame.cpp:223.
                            v8_error::ThrowError(iso, "That dialog is not currently clickable.");
                        }
                    },
                    v8_convert::ToV8(isolate, lines[i].text));
                auto handlerFunc = handlerTpl->GetFunction(context).ToLocalChecked();

                auto obj = v8::Object::New(isolate);
                obj->Set(context, v8_convert::ToV8(isolate, "text"), v8_convert::ToV8(isolate, lines[i].text)).Check();
                obj->Set(context, v8_convert::ToV8(isolate, "selectable"),
                         v8_convert::ToV8(isolate, lines[i].isSelectable))
                    .Check();
                obj->Set(context, v8_convert::ToV8(isolate, "handler"), handlerFunc).Check();
                array->Set(context, i, obj).Check();
            }

            args.GetReturnValue().Set(array);
        });

    /// @description Simulate a map click at a unit's location or at world coordinates.
    /// @signature clickMap(clickType: number, shift: number|boolean, unit: Unit)
    /// @param clickType {number} - click type id
    /// @param shift {number|boolean} - whether shift is held (coerced to bool)
    /// @param unit {Unit} - target Unit
    /// @signature clickMap(clickType: number, shift: number|boolean, x: number, y: number)
    /// @param x {number} - target x coordinate
    /// @param y {number} - target y coordinate
    /// @returns {boolean} - true if the click dispatched, false on bad args / invalid target
    v8_function::Register(
        isolate, global, "clickMap", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            // Reference signature: clickMap(clickType, shift, x, y)
            //                    or clickMap(clickType, shift, unitObj)
            if (args.Length() < 3) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint32_t clickType = v8_convert::ToUint32(isolate, args[0]);
            bool shift = false;
            if (args[1]->IsNumber() || args[1]->IsBoolean()) {
                shift = v8_convert::ToBool(isolate, args[1]);
            }

            // Unit object overload: clickMap(clickType, shift, unitObj)
            if (args.Length() == 3 && args[2]->IsObject()) {
                auto unitObj = args[2].As<v8::Object>();
                if (JSUnit::IsInstance(unitObj)) {
                    auto* unitData = JSUnit::Unwrap(unitObj);
                    if (!unitData || !*unitData) {
                        args.GetReturnValue().SetFalse();
                        return;
                    }
                    // Pass the unit to the unit-overload of ClickMapAt
                    args.GetReturnValue().Set(d2bs::game::ClickMapAt(clickType, shift, *unitData));
                    return;
                }
                args.GetReturnValue().SetFalse();
                return;
            }

            // Coordinate overload: clickMap(clickType, shift, x, y)
            if (args.Length() >= 4 && args[2]->IsNumber() && args[3]->IsNumber()) {
                auto pos = v8_extract::Point(args, 2).value_or(d2bs::game::Point::Zero);
                args.GetReturnValue().Set(d2bs::game::ClickMapAt(clickType, shift, pos));
                return;
            }

            args.GetReturnValue().SetFalse();
        });

    /// @description Equip an item, click a body slot, click an item by handle, or click a container grid slot.
    /// @signature clickItem(item: Unit)
    /// @param item {Unit} - item Unit; equipped into the player body slot its data points to
    /// @signature clickItem(clickType: number, bodyLoc: number)
    /// @param clickType {number} - 0 = player body slot, 4 = merc body slot; any other value is a no-op that returns
    /// true
    /// @param bodyLoc {number} - body location id
    /// @signature clickItem(clickType: number, item: Unit)
    /// @signature clickItem(button: number, x: number, y: number, location: number)
    /// @param button {number} - click button id
    /// @param x {number} - container grid x
    /// @param y {number} - container grid y
    /// @param location {number} - item container/location id
    /// @returns {boolean|null} - true/null/false per shape (false while a trade is open); throws "Object is not an
    /// item!" for item-by-handle on a non-item
    /// @throws {Error} - item-by-handle shape is used on a unit that is not an item
    v8_function::Register(
        isolate, global, "clickItem", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            // clickItem overloads (from ref JSGame.cpp:357-661):
            //   (item:unit)                 -- click the player body slot that the item's
            //                                  pItemData->BodyLocation points to
            //   (0, bodyLoc:int)            -- click player body slot directly
            //   (4, bodyLoc:int)            -- click merc body slot (bodyLoc in {1,3,4})
            //   (clickType:int, unit)       -- click an item by handle (inv/stash/cube/belt/
            //                                  cursor/merc-owned dispatch)
            //   (button, x, y, location)    -- grid-coord click in a container
            //
            // Per-shape rval table (matches ref for non-transaction paths; if a transaction
            // is open, the abstraction returns TransactionInProgress and we emit null/true
            // following the shape's normal mapping instead of ref's false -- we don't try
            // to achieve exact transaction-state parity for invalid argshapes):
            //                         Dispatched  Transaction  Invalid  NotAnItem
            //   PlayerBodySlotByItem       null       see note    null      null
            //   PlayerBodySlot             null       see note    null      n/a
            //   MercBodySlot               true       null         null      n/a
            //   ItemByHandle               true       null         null      throw
            //   ContainerGrid              true       null         null      n/a
            //   Unrecognized               n/a        n/a          n/a       n/a  (always true)

            enum class Shape : uint8_t {
                PlayerBodySlotByItem,  // (item)                       ref case A
                PlayerBodySlot,        // (0, bodyLoc)                 ref case B, clickType==0
                MercBodySlot,          // (4, bodyLoc)                 ref case B, clickType==4
                ItemByHandle,          // (clickType, unit)            ref case C
                ContainerGrid,         // (button, x, y, location)     ref case D
                Unrecognized,
            };

            Shape shape = Shape::Unrecognized;
            if (args.Length() == 1 && args[0]->IsObject()) {
                shape = Shape::PlayerBodySlotByItem;
            } else if (args.Length() == 2 && args[0]->IsNumber() && args[1]->IsNumber()) {
                // Ref only acts on clickType 0 (player body) and 4 (merc body). Other
                // values (1, 2, 3, 5+) fall through to ref's generic rval=true path --
                // route them to Unrecognized so we don't dispatch a spurious body click.
                uint32_t clickType = v8_convert::ToUint32(isolate, args[0]);
                if (clickType == 0) {
                    shape = Shape::PlayerBodySlot;
                } else if (clickType == 4) {
                    shape = Shape::MercBodySlot;
                }
            } else if (args.Length() == 2 && args[0]->IsNumber() && args[1]->IsObject()) {
                shape = Shape::ItemByHandle;
            } else if (args.Length() == 4 && args[0]->IsNumber() && args[1]->IsNumber() && args[2]->IsNumber() &&
                       args[3]->IsNumber()) {
                shape = Shape::ContainerGrid;
            }

            using d2bs::game::ClickResult;

            if (shape == Shape::Unrecognized) {
                // Ref falls through to rval=true at line 658 for unrecognized shapes.
                args.GetReturnValue().Set(true);
                return;
            }

            // Shape-specific dispatch. Invalid wrappers short-circuit with the shape's
            // natural failure rval (null or throw), skipping the abstraction entirely.
            ClickResult result = ClickResult::InvalidTarget;

            auto lock = d2bs::game::Bridge::Lock();
            switch (shape) {
                case Shape::PlayerBodySlotByItem: {
                    auto obj = args[0].As<v8::Object>();
                    if (!JSUnit::IsInstance(obj)) {
                        args.GetReturnValue().SetNull();  // ref case A returns null here.
                        return;
                    }
                    auto* data = JSUnit::Unwrap(obj);
                    if (!data || !*data) {
                        args.GetReturnValue().SetNull();  // ref case A returns null here too.
                        return;
                    }
                    result = data->EquipItem();
                    break;
                }
                case Shape::PlayerBodySlot: {
                    auto slot = static_cast<game::BodyLocation>(v8_convert::ToUint32(isolate, args[1]));
                    result = d2bs::game::ClickBodyLocation(slot, game::InventoryOwner::Player);
                    break;
                }
                case Shape::MercBodySlot: {
                    auto slot = static_cast<game::BodyLocation>(v8_convert::ToUint32(isolate, args[1]));
                    result = d2bs::game::ClickBodyLocation(slot, game::InventoryOwner::Mercenary);
                    break;
                }
                case Shape::ItemByHandle: {
                    auto obj = args[1].As<v8::Object>();
                    if (!JSUnit::IsInstance(obj)) {
                        // Ref case C line 458-461: private-type fail returns null (no throw).
                        args.GetReturnValue().SetNull();
                        return;
                    }
                    auto* data = JSUnit::Unwrap(obj);
                    if (!data || !*data) {
                        // Ref case C line 467-470: findUnit-fail / dwType-mismatch throws.
                        v8_error::ThrowError(isolate, "Object is not an item!");
                        return;
                    }
                    auto button = static_cast<game::ClickButton>(v8_convert::ToUint32(isolate, args[0]));
                    result = d2bs::game::ClickItem(button, *data);
                    break;
                }
                case Shape::ContainerGrid: {
                    auto button = static_cast<game::ClickButton>(v8_convert::ToUint32(isolate, args[0]));
                    auto gridPos = v8_extract::Position(args, 1).value_or(d2bs::game::Position::Zero);
                    auto location = static_cast<game::ItemLocation>(v8_convert::ToUint32(isolate, args[3]));
                    result = d2bs::game::ClickContainerSlot(button, gridPos, location);
                    break;
                }
                case Shape::Unrecognized:
                    break;  // handled by the early return above.
            }

            // Transaction dialog blocks all shapes with false (matches ref for the cases
            // we reach this point -- valid argshapes + valid wrapper).
            if (result == ClickResult::TransactionInProgress) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Per-shape rval mapping.
            switch (shape) {
                case Shape::PlayerBodySlotByItem:
                case Shape::PlayerBodySlot:
                    // Ref never upgrades initial null to true in these shapes.
                    // NotAnItem from the abstraction (non-item unit in shape A) also maps
                    // to null -- ref case A has undefined behavior for non-items; null is
                    // the safe observable match.
                    args.GetReturnValue().SetNull();
                    break;
                case Shape::MercBodySlot:
                case Shape::ContainerGrid:
                    if (result == ClickResult::Dispatched) {
                        args.GetReturnValue().Set(true);
                    } else {
                        args.GetReturnValue().SetNull();
                    }
                    break;
                case Shape::ItemByHandle:
                    // ref case C line 469: THROW_ERROR for non-items.
                    if (result == ClickResult::NotAnItem) {
                        v8_error::ThrowError(isolate, "Object is not an item!");
                        return;
                    }
                    if (result == ClickResult::Dispatched) {
                        args.GetReturnValue().Set(true);
                    } else {
                        args.GetReturnValue().SetNull();
                    }
                    break;
                case Shape::Unrecognized:
                    break;  // handled by the early return above.
            }
        });

    /// @description Perform a party action (invite/leave/loot/hostile) on a party member.
    /// @signature clickParty(party: Party, mode: number)
    /// @param party {Party} - the target party member object (cannot be yourself)
    /// @param mode {number} - party action mode: 0 = allow loot, 1 = unhostile, 2 = invite, 3 = leave, 4 = hostile, 5 =
    /// hostile (alt)
    /// @returns {boolean} - true if the action dispatched, false on bad args or no-op conditions
    v8_function::Register(
        isolate, global, "clickParty", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 2) {
                args.GetReturnValue().SetFalse();
                return;
            }

            if (!args[0]->IsObject() || !args[1]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto partyObj = args[0].As<v8::Object>();
            if (!JSParty::IsInstance(partyObj)) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* partyData = JSParty::Unwrap(partyObj);
            if (!partyData || !*partyData) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto mode = static_cast<d2bs::game::PartyMode>(v8_convert::ToUint32(isolate, args[1]));

            // Mode range check (reference supports modes 0-5)
            if (mode > d2bs::game::PartyMode::HostileAlt) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Prevent clicking self
            auto player = d2bs::game::Unit::Player();
            if (player && partyData->Id() == player.Id()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Reference JSGame.cpp:1104 - AllowLoot is a no-op in non-hardcore games.
            if (mode == d2bs::game::PartyMode::AllowLoot &&
                !(d2bs::game::GetCharFlags() & d2bs::game::CHAR_FLAG_HARDCORE)) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Resolve the player's roster entry once for the party-id checks below.
            // Reference reads `mypUnit->wPartyId`; we obtain it via the same roster walk.
            std::optional<uint16_t> playerPartyId;
            if (player) {
                if (auto playerRoster = d2bs::game::Party::FindById(player.Id())) {
                    playerPartyId = playerRoster->PartyId();
                }
            }
            constexpr uint16_t NO_PARTY = 0xFFFF;

            // Reference JSGame.cpp:1108 - Invite no-ops if both are already in the same party.
            if (mode == d2bs::game::PartyMode::Invite && partyData->PartyId() != NO_PARTY && playerPartyId &&
                *playerPartyId == partyData->PartyId()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // Reference JSGame.cpp:1112 - Leave no-ops if the target unit isn't in a party.
            if (mode == d2bs::game::PartyMode::Leave && partyData->PartyId() == NO_PARTY) {
                args.GetReturnValue().SetFalse();
                return;
            }

            if (mode == d2bs::game::PartyMode::Leave) {
                d2bs::game::LeaveParty();
            } else {
                d2bs::game::ClickPartyMember(*partyData, mode);
            }
            args.GetReturnValue().Set(true);
        });

    /// @description Send one or more chat messages, one per argument.
    /// @signature say(...messages: any)
    /// @param messages {any} - zero or more values; each is stringified and sent as a chat line (null/undefined values
    /// skipped)
    /// @returns {boolean} - always true
    v8_function::Register(
        isolate, global, "say", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Reference: no early return for zero args - loop simply doesn't execute, returns true
            for (int32_t i = 0; i < args.Length(); ++i) {
                if (args[i]->IsNullOrUndefined())
                    continue;
                std::string text = v8_convert::ToString(isolate, args[i]);
                d2bs::game::Say(text);
            }
            args.GetReturnValue().Set(true);
        });

    /// @description Prints a message to the in-game message area (the scrolling text in the top-left of the screen),
    /// optionally colored.
    /// @signature printGameString(text: string, color?: number)
    /// @param text {string} - the text to display (coerced to a string)
    /// @param color {number} - optional D2 message color code (default 0)
    /// @returns {boolean} - true if a message was printed; false if no text argument was provided
    v8_function::Register(
        isolate, global, "printGameString", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || args[0]->IsNullOrUndefined()) {
                args.GetReturnValue().Set(false);
                return;
            }
            std::string text = v8_convert::ToString(isolate, args[0]);
            int32_t color = args.Length() > 1 ? v8_convert::ToInt32(isolate, args[1]) : 0;
            d2bs::game::PrintGameString(text, color);
            args.GetReturnValue().Set(true);
        });

    /// @description Play an in-game sound by id.
    /// @signature playSound(soundId: number)
    /// @param soundId {number} - game sound id
    /// @returns {boolean} - true if played, false if missing/non-numeric arg
    v8_function::Register(
        isolate, global, "playSound", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint32_t soundId = v8_convert::ToUint32(isolate, args[0]);
            d2bs::game::PlayGameSound(soundId);
            args.GetReturnValue().Set(true);
        });

    /// @description Exit the current game back to the menu (does not terminate the process).
    /// @signature quit()
    /// @returns {boolean} - always false
    v8_function::Register(
        isolate, global, "quit", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Reference line 1022: JS_SET_RVAL(cx, vp, JSVAL_FALSE)
            if (d2bs::game::IsInGame()) {
                d2bs::game::ExitGame();
            }
            args.GetReturnValue().SetFalse();
        });

    /// @description Exit the current game and then terminate the entire process immediately.
    /// @signature quitGame()
    /// @returns {boolean} - false (unreachable; process terminates first)
    v8_function::Register(
        isolate, global, "quitGame", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Reference line 1010: JS_SET_RVAL(cx, vp, JSVAL_FALSE)
            if (d2bs::game::IsInGame()) {
                d2bs::game::ExitGame();
            }
            TerminateProcess(GetCurrentProcess(), 0);
            args.GetReturnValue().SetFalse();
        });

    /// @description Trigger the Horadric Cube transmute action.
    /// @signature transmute()
    /// @returns {null} - null
    v8_function::Register(
        isolate, global, "transmute", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            d2bs::game::Transmute();
            args.GetReturnValue().SetNull();
        });

    /// @description Swap the weapon switch, or query the current switch state.
    /// @signature weaponSwitch(query?: number)
    /// @param query {number} - 0/omitted performs the swap, non-zero returns the current switch state
    /// @returns {boolean|number} - true if swapped (false in classic D2), or the current switch state when querying
    v8_function::Register(
        isolate, global, "weaponSwitch", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            int32_t nParameter = 0;
            if (args.Length() > 0) {
                nParameter = v8_convert::ToInt32(isolate, args[0]);
            }

            if (nParameter != 0) {
                // Return current weapon switch state
                args.GetReturnValue().Set(static_cast<int32_t>(d2bs::game::GetWeaponSwitch()));
            } else {
                // Reference JSGame.cpp:1243 - classic D2 has no weapon switch.
                if (!(d2bs::game::GetCharFlags() & d2bs::game::CHAR_FLAG_EXPAC)) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                d2bs::game::SwapWeapon();
                args.GetReturnValue().Set(true);
            }
        });

    /// @description Spend unallocated stat points on an attribute.
    /// @signature useStatPoint(stat: number, count?: number)
    /// @param stat {number} - stat/attribute id
    /// @param count {number} - number of points to spend (default 1)
    /// @returns {undefined} - no return value
    v8_function::Register(
        isolate, global, "useStatPoint", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 1) {
                v8_error::ThrowTypeError(isolate, "useStatPoint requires at least 1 argument");
                return;
            }

            uint32_t stat = v8_convert::ToUint32(isolate, args[0]);
            int32_t count = 1;
            if (args.Length() > 1) {
                count = v8_convert::ToInt32(isolate, args[1]);
            }
            d2bs::game::UseStatPoint(stat, count);
        });

    /// @description Spend unallocated skill points on a skill.
    /// @signature useSkillPoint(skill: number, count?: number)
    /// @param skill {number} - skill id
    /// @param count {number} - number of points to spend (default 1)
    /// @returns {undefined} - no return value
    v8_function::Register(
        isolate, global, "useSkillPoint", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 1) {
                v8_error::ThrowTypeError(isolate, "useSkillPoint requires at least 1 argument");
                return;
            }

            uint32_t skill = v8_convert::ToUint32(isolate, args[0]);
            int32_t count = 1;
            if (args.Length() > 1) {
                count = v8_convert::ToInt32(isolate, args[1]);
            }
            d2bs::game::UseSkillPoint(skill, count);
        });

    /// @description Capture a screenshot of the game.
    /// @signature takeScreenshot()
    /// @returns {undefined} - no return value
    v8_function::Register(
        isolate, global, "takeScreenshot",
        +[](const v8::FunctionCallbackInfo<v8::Value>& args) { d2bs::game::TakeScreenshot(); });

    /// @description Copy a string to the Windows clipboard (CF_TEXT); best-effort, no-op on failure.
    /// @signature copy(text: string)
    /// @param text {string} - text to place on the clipboard (any value is stringified)
    /// @returns {undefined} - no return value
    v8_function::Register(
        isolate, global, "copy", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1) {
                v8_error::ThrowTypeError(isolate, "copy requires 1 argument");
                return;
            }

            std::string text = v8_convert::ToString(isolate, args[0]);

            HGLOBAL hText = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, text.size() + 1);
            if (hText) {
                auto* pText = static_cast<char*>(GlobalLock(hText));
                if (pText) {
                    std::memcpy(pText, text.data(), text.size() + 1);
                    GlobalUnlock(hText);
                    if (OpenClipboard(nullptr)) {
                        EmptyClipboard();
                        SetClipboardData(CF_TEXT, hText);
                        CloseClipboard();
                    } else {
                        GlobalFree(hText);
                    }
                } else {
                    GlobalFree(hText);
                }
            }
        });

    /// @description Read text from the Windows clipboard (CF_TEXT).
    /// @signature paste()
    /// @returns {string|undefined} - clipboard text, empty string if no text data, or undefined if clipboard
    /// unavailable
    v8_function::Register(
        isolate, global, "paste", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!OpenClipboard(nullptr)) {
                return;  // Returns undefined -- distinguishable from empty clipboard
            }
            HANDLE hData = GetClipboardData(CF_TEXT);
            if (hData) {
                auto* pText = static_cast<const char*>(GlobalLock(hData));
                if (pText) {
                    std::string text(pText);
                    GlobalUnlock(hData);
                    CloseClipboard();
                    args.GetReturnValue().Set(v8_convert::ToV8(isolate, text));
                    return;
                }
            }
            CloseClipboard();
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, ""));
        });

    /// @description Send an inter-process WM_COPYDATA message to another window/game instance.
    /// @signature sendCopyData(windowClass: string|null, hwndOrName: number|string, mode: number, data: string)
    /// @param windowClass {string|null} - target window class name, or null/undefined for none
    /// @param hwndOrName {number|string} - target window handle (number) or window name (string)
    /// @param mode {number} - IPC mode id
    /// @param data {string} - data payload (empty unless a string is passed)
    /// @returns {boolean} - true if the IPC send succeeded, false otherwise
    v8_function::Register(
        isolate, global, "sendCopyData", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 4) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // arg0: window class name (string or null)
            std::string windowClassName;
            if (!args[0]->IsNullOrUndefined() && args[0]->IsString()) {
                windowClassName = v8_convert::ToString(isolate, args[0]);
            }

            // arg1: HWND (number) or window name (string)
            uintptr_t hwnd = 0;
            std::string windowName;
            if (args[1]->IsNumber()) {
                // Handles are pointer-sized: read as double to avoid truncation on x64.
                hwnd = static_cast<uintptr_t>(v8_convert::ToDouble(isolate, args[1]));
            } else if (args[1]->IsString()) {
                windowName = v8_convert::ToString(isolate, args[1]);
            }

            // arg2: mode ID, arg3: data
            uint32_t modeId = v8_convert::ToUint32(isolate, args[2]);
            std::string data;
            if (args[3]->IsString()) {
                data = v8_convert::ToString(isolate, args[3]);
            }

            args.GetReturnValue().Set(d2bs::game::SendIPC(modeId, data, hwnd, windowClassName, windowName));
        });

    /// @description Check whether a virtual key is currently down (GetAsyncKeyState).
    /// @signature keystate(vk: number)
    /// @param vk {number} - Windows virtual-key code
    /// @returns {boolean} - true if the key is currently pressed
    v8_function::Register(
        isolate, global, "keystate", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            int32_t vk = v8_convert::ToInt32(isolate, args[0]);
            args.GetReturnValue().Set(!!GetAsyncKeyState(vk));
        });

    /// @description Get the current mouse cursor coordinates, optionally converted to world coords.
    /// @signature getMouseCoords(toWorld?: boolean, asObject?: boolean)
    /// @param toWorld {boolean} - true converts screen coords to world coords (default false)
    /// @param asObject {boolean} - true returns {x, y}, false/omitted returns [x, y] (default false)
    /// @returns {Array<number>|{x:number,y:number}} - mouse coords as array or object
    v8_function::Register(
        isolate, global, "getMouseCoords", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            bool toWorld = false;
            if (args.Length() > 0) {
                toWorld = v8_convert::ToBool(isolate, args[0]);
            }
            bool asObject = false;
            if (args.Length() > 1) {
                asObject = v8_convert::ToBool(isolate, args[1]);
            }

            auto mouse = d2bs::game::GetMousePos();
            auto p = mouse.ToPoint();

            if (toWorld) {
                p = d2bs::game::AbsScreenToMap(p);
            }

            if (asObject) {
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, p));
            } else {
                auto arr = v8::Array::New(isolate, 2);
                arr->Set(context, 0, v8_convert::ToV8(isolate, p.x)).Check();
                arr->Set(context, 1, v8_convert::ToV8(isolate, p.y)).Check();
                args.GetReturnValue().Set(arr);
            }
        });

    /// @description Convert screen coordinates to automap coordinates.
    /// @signature screenToAutomap(point: {x:number,y:number})
    /// @param point {object} - a {x, y} object
    /// @signature screenToAutomap(x: number, y: number)
    /// @param x {number} - screen x coordinate
    /// @param y {number} - screen y coordinate
    /// @returns {{x:number,y:number}} - converted automap coordinates
    v8_function::Register(
        isolate, global, "screenToAutomap", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1) {
                v8_error::ThrowTypeError(isolate, "screenToAutomap requires at least 1 argument");
                return;
            }

            // Strict: reject non-numeric input (string->int coercion silently corrupted coords).
            auto p = d2bs::game::Point::Zero;
            if (args.Length() == 1 && args[0]->IsObject()) {
                auto extracted = v8_extract::Point(isolate, args[0]);
                if (!extracted) {
                    v8_error::ThrowTypeError(isolate, "Input has an x or y, but they aren't the correct type!");
                    return;
                }
                p = *extracted;
            } else if (args.Length() >= 2 && args[0]->IsNumber() && args[1]->IsNumber()) {
                p = v8_extract::Point(args, 0).value_or(p);
            } else {
                v8_error::ThrowTypeError(isolate, "Invalid arguments for screenToAutomap");
                return;
            }

            p = d2bs::game::ScreenToAutomap(p);

            args.GetReturnValue().Set(v8_convert::ToV8(isolate, p));
        });

    /// @description Convert automap coordinates to screen coordinates.
    /// @signature automapToScreen(point: {x:number,y:number})
    /// @param point {object} - a {x, y} object
    /// @signature automapToScreen(x: number, y: number)
    /// @param x {number} - automap x coordinate
    /// @param y {number} - automap y coordinate
    /// @returns {{x:number,y:number}} - converted screen coordinates
    v8_function::Register(
        isolate, global, "automapToScreen", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1) {
                v8_error::ThrowTypeError(isolate, "automapToScreen requires at least 1 argument");
                return;
            }

            // Strict: reject non-numeric input (string->int coercion silently corrupted coords).
            auto p = d2bs::game::Point::Zero;
            if (args.Length() == 1 && args[0]->IsObject()) {
                auto extracted = v8_extract::Point(isolate, args[0]);
                if (!extracted) {
                    v8_error::ThrowTypeError(isolate, "Input has an x or y, but they aren't the correct type!");
                    return;
                }
                p = *extracted;
            } else if (args.Length() >= 2 && args[0]->IsNumber() && args[1]->IsNumber()) {
                p = v8_extract::Point(args, 0).value_or(p);
            } else {
                v8_error::ThrowTypeError(isolate, "Invalid arguments for automapToScreen");
                return;
            }

            p = d2bs::game::AutomapToScreen(p);

            args.GetReturnValue().Set(v8_convert::ToV8(isolate, p));
        });

    /// @description Compute the Euclidean distance between two points, units, or the player and a target.
    /// @signature getDistance(x1: number, y1: number, x2: number, y2: number)
    /// @param x1 {number} - first point x
    /// @param y1 {number} - first point y
    /// @param x2 {number} - second point x
    /// @param y2 {number} - second point y
    /// @signature getDistance(obj: Unit|{x:number,y:number})
    /// @param obj {Unit|object} - distance from the player to this Unit (uses its position) or {x, y} object
    /// @signature getDistance(x: number, y: number)
    /// @param x {number} - a coordinate x; distance from the player to (x, y)
    /// @param y {number} - a coordinate y
    /// @signature getDistance(a: Unit|{x:number,y:number}, b: Unit|{x:number,y:number})
    /// @param a {Unit|object} - first object
    /// @param b {Unit|object} - second object
    /// @signature getDistance(obj: Unit|{x:number,y:number}, x: number, y: number)
    /// @signature getDistance(x: number, y: number, obj: Unit|{x:number,y:number})
    /// @returns {number} - the distance, or 0 if no args / a point is unresolved
    v8_function::Register(
        isolate, global, "getDistance", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            args.GetReturnValue().Set(0.0);

            if (args.Length() < 1) {
                return;
            }

            // Helper: extract a Point from an object -- either JSUnit or plain {x, y}
            auto getObjectPoint = [&](v8::Local<v8::Object> obj) -> std::optional<d2bs::game::Point> {
                if (JSUnit::IsInstance(obj)) {
                    auto* unitData = JSUnit::Unwrap(obj);
                    if (!unitData || !*unitData) {
                        return std::nullopt;
                    }
                    return unitData->Pos().ToPoint();
                }
                return v8_extract::Point(isolate, obj);
            };

            // (x1, y1, x2, y2) -- pure coordinate distance
            if (args.Length() == 4 && args[0]->IsNumber() && args[1]->IsNumber() && args[2]->IsNumber() &&
                args[3]->IsNumber()) {
                auto p1 = v8_extract::Point(args, 0).value_or(d2bs::game::Point::Zero);
                auto p2 = v8_extract::Point(args, 2).value_or(d2bs::game::Point::Zero);
                args.GetReturnValue().Set(Distance(p1, p2));
                return;
            }

            // (obj) -- distance from player to object (JSUnit or {x,y})
            if (args.Length() == 1 && args[0]->IsObject()) {
                auto p2 = getObjectPoint(args[0].As<v8::Object>());
                if (p2) {
                    auto player = d2bs::game::Unit::Player();
                    if (player) {
                        auto p1 = player.Pos().ToPoint();
                        args.GetReturnValue().Set(Distance(p1, *p2));
                        return;
                    }
                }
            }

            if (args.Length() == 2) {
                // (x, y) -- distance from player to point
                if (args[0]->IsNumber() && args[1]->IsNumber()) {
                    auto player = d2bs::game::Unit::Player();
                    if (!player) {
                        return;
                    }
                    auto p1 = player.Pos().ToPoint();
                    auto p2 = v8_extract::Point(args, 0).value_or(d2bs::game::Point::Zero);
                    args.GetReturnValue().Set(Distance(p1, p2));
                    return;
                }
                // (obj, obj) -- distance between two objects
                if (args[0]->IsObject() && args[1]->IsObject()) {
                    auto p1 = getObjectPoint(args[0].As<v8::Object>());
                    auto p2 = getObjectPoint(args[1].As<v8::Object>());
                    if (p1 && p2) {
                        args.GetReturnValue().Set(Distance(*p1, *p2));
                        return;
                    }
                }
            }

            if (args.Length() == 3) {
                // (obj, x, y) -- distance from object to coords
                if (args[0]->IsObject() && args[1]->IsNumber() && args[2]->IsNumber()) {
                    auto p1 = getObjectPoint(args[0].As<v8::Object>());
                    if (p1) {
                        auto p2 = v8_extract::Point(args, 1).value_or(d2bs::game::Point::Zero);
                        args.GetReturnValue().Set(Distance(*p1, p2));
                        return;
                    }
                }
                // (x, y, obj) -- distance from coords to object
                if (args[0]->IsNumber() && args[1]->IsNumber() && args[2]->IsObject()) {
                    auto p2 = getObjectPoint(args[2].As<v8::Object>());
                    if (p2) {
                        auto p1 = v8_extract::Point(args, 0).value_or(d2bs::game::Point::Zero);
                        args.GetReturnValue().Set(Distance(p1, *p2));
                        return;
                    }
                }
            }
        });

    /// @description Perform a gold action (e.g. drop/stash/withdraw).
    /// @signature gold(amount?: number, mode?: number)
    /// @param amount {number} - gold amount (default 0)
    /// @param mode {number} - gold action mode (default Stash)
    /// @returns {undefined} - no return value
    v8_function::Register(
        isolate, global, "gold", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            int32_t nGold = 0;
            auto nMode = d2bs::game::GoldActionMode::Stash;
            if (args.Length() > 0 && args[0]->IsNumber()) {
                nGold = v8_convert::ToInt32(isolate, args[0]);
            }
            if (args.Length() > 1 && args[1]->IsNumber()) {
                nMode = static_cast<d2bs::game::GoldActionMode>(v8_convert::ToInt32(isolate, args[1]));
            }
            d2bs::game::GoldAction(nMode, nGold);
        });

    /// @description Play a Windows system beep (MessageBeep).
    /// @signature beep(beepId?: number)
    /// @param beepId {number} - MessageBeep type id (default 0)
    /// @returns {boolean} - always true
    v8_function::Register(
        isolate, global, "beep", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            int32_t beepId = 0;
            if (args.Length() > 0 && args[0]->IsNumber()) {
                beepId = v8_convert::ToInt32(args.GetIsolate(), args[0]);
            }
            MessageBeep(static_cast<UINT>(beepId));
            args.GetReturnValue().Set(true);
        });

    /// @description Submit the item currently held on the cursor (e.g. to an NPC for socketing or imbue).
    /// @signature submitItem()
    /// @returns {boolean} - true if the item was submitted, false if no cursor item
    v8_function::Register(
        isolate, global, "submitItem", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto cursorItem = d2bs::game::Unit::CursorItem();
            if (!cursorItem) {
                args.GetReturnValue().SetFalse();
                return;
            }

            args.GetReturnValue().Set(d2bs::game::SubmitItem(*cursorItem));
        });

    /// @description Create a fresh Unit handle referring to the same game unit as an existing Unit object.
    /// @signature copyUnit(unit: Unit)
    /// @param unit {Unit} - the Unit object to copy
    /// @returns {Unit|undefined} - a new Unit handle, or undefined if the arg is not a valid Unit
    v8_function::Register(
        isolate, global, "copyUnit", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!v8_error::CheckArgCount(args, 1, "copyUnit")) {
                return;
            }

            if (!args[0]->IsObject()) {
                return;
            }

            auto unitObj = args[0].As<v8::Object>();
            if (!JSUnit::IsInstance(unitObj)) {
                return;
            }

            auto* srcUnit = JSUnit::Unwrap(unitObj);
            if (!srcUnit) {
                return;
            }

            args.GetReturnValue().Set(
                JSUnit::CreateInstance(isolate, context, std::make_unique<d2bs::game::Unit>(*srcUnit)));
        });

    /// @description Accept a trade, or query trade state by mode.
    /// @signature acceptTrade(mode?: number)
    /// @param mode {number} - query: 1 = is accepted, 2 = recent trade id, 3 = is blocked; omit/other to accept
    /// @returns {boolean|number} - query result (bool or id) for query modes, else the accept result (bool)
    v8_function::Register(
        isolate, global, "acceptTrade", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto lock = d2bs::game::Bridge::Lock();
            if (args.Length() > 0) {
                auto mode = static_cast<d2bs::game::AcceptTradeQueryMode>(v8_convert::ToInt32(isolate, args[0]));
                if (mode == d2bs::game::AcceptTradeQueryMode::IsAccepted) {
                    // Reference: BOOLEAN_TO_JSVAL(*p_D2CLIENT_bTradeAccepted)
                    args.GetReturnValue().Set(d2bs::game::IsTradeAccepted());
                    return;
                }
                if (mode == d2bs::game::AcceptTradeQueryMode::RecentTradeId) {
                    // Reference: INT_TO_JSVAL(*p_D2CLIENT_RecentTradeId)
                    args.GetReturnValue().Set(d2bs::game::GetRecentTradeId());
                    return;
                }
                if (mode == d2bs::game::AcceptTradeQueryMode::IsBlocked) {
                    // Reference: BOOLEAN_TO_JSVAL(*p_D2CLIENT_bTradeBlock)
                    args.GetReturnValue().Set(d2bs::game::IsTradeBlocked());
                    return;
                }
            }

            args.GetReturnValue().Set(d2bs::game::AcceptTrade());
        });

    /// @description Click OK in the trade window.
    /// @signature tradeOk()
    /// @returns {undefined} - no return value; throws Error if not in a state to click OK
    /// @throws {Error} - not in a proper state to click OK in the trade window
    v8_function::Register(
        isolate, global, "tradeOk", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            auto lock = d2bs::game::Bridge::Lock();
            if (!d2bs::game::TradeOK()) {
                v8_error::ThrowError(isolate, "Not in proper state to click ok to trade.");
            }
        });

    /// @description Check collision between two units against a mask.
    /// @signature checkCollision(unit1: Unit, unit2: Unit, mask: number)
    /// @param unit1 {Unit} - first unit object
    /// @param unit2 {Unit} - second unit object
    /// @param mask {number} - collision mask
    /// @returns {number|undefined} - collision result, or undefined on invalid args / unresolved units
    v8_function::Register(
        isolate, global, "checkCollision", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 3) {
                return;
            }

            if (!args[0]->IsObject() || !args[1]->IsObject()) {
                return;
            }

            auto unitObj1 = args[0].As<v8::Object>();
            auto unitObj2 = args[1].As<v8::Object>();

            if (!JSUnit::IsInstance(unitObj1) || !JSUnit::IsInstance(unitObj2)) {
                return;
            }

            auto* unitData1 = JSUnit::Unwrap(unitObj1);
            auto* unitData2 = JSUnit::Unwrap(unitObj2);
            if (!unitData1 || !*unitData1 || !unitData2 || !*unitData2) {
                return;
            }

            uint32_t mask = v8_convert::ToUint32(isolate, args[2]);
            args.GetReturnValue().Set(d2bs::game::CheckUnitCollision(*unitData1, *unitData2, mask));
        });

    /// @description Teleport/move a monster NPC to a position; requires enableUnsupported = true in config.
    /// @signature moveNPC(unit: Unit, x: number, y: number)
    /// @param unit {Unit} - the monster/NPC unit to move (must be a monster type)
    /// @param x {number} - destination x coordinate
    /// @param y {number} - destination y coordinate
    /// @returns {undefined} - no return value; throws Error on a non-monster unit or bad args
    /// @throws {Error} - the unit is not a monster type
    v8_function::Register(
        isolate, global, "moveNPC", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::WaitForGameReady(d2bs::config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (!d2bs::config::GetAppConfig().enableUnsupported.load()) {
                v8_error::WarnAndReturnFalse(args, "moveNPC requires enableUnsupported = true in config");
                return;
            }

            if (args.Length() < 3) {
                v8_error::ThrowError(isolate, "moveNPC requires 3 arguments");
                return;
            }

            if (!args[0]->IsObject()) {
                v8_error::ThrowError(isolate, "moveNPC requires a unit object as first argument");
                return;
            }

            auto unitObj = args[0].As<v8::Object>();
            if (!JSUnit::IsInstance(unitObj)) {
                v8_error::ThrowError(isolate, "moveNPC requires a unit object as first argument");
                return;
            }

            auto* unitData = JSUnit::Unwrap(unitObj);
            if (!unitData || !*unitData) {
                return;
            }

            // Reference line 1435: unit must be a monster/NPC (type 1)
            if (unitData->Type() != d2bs::game::UnitType::Monster) {
                v8_error::ThrowError(isolate, "Invalid NPC passed to moveNPC!");
                return;
            }

            auto pos = v8_extract::Position(args, 1).value_or(d2bs::game::Position::Zero);

            d2bs::game::MoveNPC(unitData->Id(), pos);
        });

    /// @description Reveal the player's current level on the automap.
    /// @signature revealLevel(drawPresets?: boolean)
    /// @param drawPresets {boolean} - true also reveals preset units (default false)
    /// @returns {undefined} - no return value
    v8_function::Register(
        isolate, global, "revealLevel", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!d2bs::game::IsGameReady()) {
                return;
            }

            bool drawPresets = false;
            if (args.Length() >= 1 && args[0]->IsBoolean()) {
                drawPresets = args[0]->BooleanValue(isolate);
            }

            auto lock = d2bs::game::Bridge::Lock();
            auto player = d2bs::game::Unit::Player();
            if (!player) {
                return;
            }

            uint32_t levelNo = player.Area();
            d2bs::game::RevealLevel(levelNo, drawPresets);
        });
}

}  // namespace d2bs::api::globals
