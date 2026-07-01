#include "JSUnit.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <optional>

#include "api/core/V8Extract.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "config/AppConfig.h"
#include "game/Bridge.h"
#include "game/Constants.h"
#include "game/Finders.h"
#include "game/GameHelpers.h"

namespace d2bs::api::classes {

namespace v8_extract = v8_extract;

using game::UnitType;

// NOLINTNEXTLINE(readability-function-size) - V8 template configuration, intentionally large
void JSUnit::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    /// @description Unit type category.
    /// @type {UnitType}
    Property(
        isolate, inst, "type", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            // No *data check as type is cached
            info.GetReturnValue().Set(static_cast<uint32_t>(data->Type()));
        });

    /// @description Class ID of the unit (monster class, item class, object class, etc.).
    /// @type {number}
    Property(
        isolate, inst, "classid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->ClassId());
        });

    /// @description Current animation/action mode of the unit (walking, attacking, dead, etc.).
    /// @type {number}
    Property(
        isolate, inst, "mode", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Mode());
        });

    /// @description Display name of the unit (player name, monster name, item name, etc.).
    /// @type {string}
    Property(
        isolate, inst, "name", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            auto* isolate = info.GetIsolate();
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->Name()));
        });

    /// @description Act the unit is currently in (1-5).
    /// @type {number}
    Property(
        isolate, inst, "act", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Act());
        });

    /// @description Global unit ID.
    /// @type {number}
    Property(
        isolate, inst, "gid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            // No *data check as unit id is usually cached
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), static_cast<double>(data->Id())));
        });

    /// @description Unit X position in world subtile coordinates.
    /// @type {number}
    Property(
        isolate, inst, "x", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Pos().x);
        });

    /// @description Unit Y position in world subtile coordinates.
    /// @type {number}
    Property(
        isolate, inst, "y", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Pos().y);
        });

    // Reference lines 239-246: only Player/Monster/Missile have pPath->xTarget; other types return undefined.
    /// @description X position of the unit's movement/path target (Player, Monster, and Missile units only).
    /// @type {number}
    Property(
        isolate, inst, "targetx", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            auto type = data->Type();
            if (type != UnitType::Player && type != UnitType::Monster && type != UnitType::Missile) {
                return;
            }
            info.GetReturnValue().Set(data->TargetPos().x);
        });

    // Reference lines 247-254: only Player/Monster/Missile have pPath->yTarget; other types return undefined.
    /// @description Y position of the unit's movement/path target (Player, Monster, and Missile units only).
    /// @type {number}
    Property(
        isolate, inst, "targety", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            auto type = data->Type();
            if (type != UnitType::Player && type != UnitType::Monster && type != UnitType::Missile) {
                return;
            }
            info.GetReturnValue().Set(data->TargetPos().y);
        });

    /// @description Area/level ID the unit is currently in.
    /// @type {number}
    Property(
        isolate, inst, "area", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Area());
        });

    /// @description Current hit points of the unit (whole-number HP, fixed-point shift already applied).
    /// @type {number}
    Property(
        isolate, inst, "hp", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Hp());
        });

    /// @description Maximum hit points of the unit (whole-number HP, fixed-point shift already applied).
    /// @type {number}
    Property(
        isolate, inst, "hpmax", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->HpMax());
        });

    /// @description Current mana points of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        isolate, inst, "mp", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Mp());
        });

    /// @description Maximum mana points of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        isolate, inst, "mpmax", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->MpMax());
        });

    /// @description Current stamina of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        isolate, inst, "stamina", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Stamina());
        });

    /// @description Maximum stamina of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        isolate, inst, "staminamax", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->StaminaMax());
        });

    /// @description Character/unit level.
    /// @type {number}
    Property(
        isolate, inst, "charlvl", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->CharLevel());
        });

    /// @description Number of items the unit owns/carries in its inventory list.
    /// @type {number}
    Property(
        isolate, inst, "itemcount", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->ItemCount());
        });

    // Emitted as int32_t so the "no owner" sentinel reaches JS as literal -1 rather than 0xFFFFFFFF.
    /// @description Owner unit ID for minions, missiles, and items (-1 when there is no owner).
    /// @type {number}
    Property(
        isolate, inst, "owner", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            auto owner = data->GetOwner();
            int32_t value = owner ? static_cast<int32_t>(owner->Id()) : -1;
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), value));
        });

    // Emitted as int32_t so the "no owner" sentinel reaches JS as literal -1.
    /// @description Unit type of this unit's owner (-1 when there is no owner).
    /// @type {number}
    Property(
        isolate, inst, "ownertype", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            auto owner = data->GetOwner();
            int32_t value = owner ? static_cast<int32_t>(owner->Type()) : -1;
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), value));
        });

    // Reference lines 280-297: only Monsters have pMonsterData; other types return undefined.
    /// @description Monster special-type bitflags (a bitfield combination); Monster units only.
    /// @type {MonsterSpecType}
    Property(
        isolate, inst, "spectype", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Monster) {
                return;
            }
            info.GetReturnValue().Set(data->SpecType());
        });

    /// @description Facing direction of the unit (game angle index, 0-63).
    /// @type {number}
    Property(
        isolate, inst, "direction", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(data->Direction());
        });

    /// @description Unique/super-unique monster ID (index into the unique monster table; -1 when none).
    /// @type {number}
    Property(
        isolate, inst, "uniqueid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            info.GetReturnValue().Set(static_cast<int32_t>(data->UniqueId().value_or(-1)));
        });

    /// @description Three/four-character item code (e.g. "rvl", "gcv"); Item units only, "Unknown" if unresolvable.
    /// @type {string}
    Property(
        isolate, inst, "code", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            auto code = data->ItemCode();
            if (code.empty()) {
                code = "Unknown";
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, code));
        });

    /// @description Localized name of the item's primary magic prefix (Item units only; empty if none).
    /// @type {string}
    Property(
        isolate, inst, "prefix", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->Prefix()));
        });

    /// @description Localized name of the item's primary magic suffix (Item units only; empty if none).
    /// @type {string}
    Property(
        isolate, inst, "suffix", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->Suffix()));
        });

    /// @description Numeric ID of the item's primary magic prefix (Item units only; 0 if none).
    /// @type {number}
    Property(
        isolate, inst, "prefixnum", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->PrefixNum()));
        });

    /// @description Numeric ID of the item's primary magic suffix (Item units only; 0 if none).
    /// @type {number}
    Property(
        isolate, inst, "suffixnum", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->SuffixNum()));
        });

    // prefixes - Sparse array of prefix names indexed by slot (3 max).
    // Reference parity (JSUnit.cpp:337-352): unset slots are JS undefined, set
    // slots carry the magic-mod string. `.length` reflects the highest set
    // index, mirroring SpiderMonkey's JS_SetElement behavior.
    /// @description Sparse array of the item's magic prefix names by slot (up to 3); Item units only.
    /// @type {string[]}
    Property(
        isolate, inst, "prefixes", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto prefixes = data->Prefixes();
            // Reference (JSUnit.cpp:337-352) starts with an empty array and
            // only `JS_SetElement`s non-empty slots, so `.length` reflects
            // (highest-set-index + 1). Match that here.
            auto arr = v8::Array::New(isolate, 0);
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by prefixes.size()
            for (uint32_t i = 0; i < prefixes.size(); ++i) {
                if (prefixes[i].has_value()) {
                    arr->Set(context, i, v8_convert::ToV8(isolate, *prefixes[i])).Check();
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(arr);
        });

    // suffixes - Sparse array of suffix names indexed by slot (3 max).
    // See `prefixes` above for sparse-hole semantics.
    /// @description Sparse array of the item's magic suffix names by slot (up to 3); Item units only.
    /// @type {string[]}
    Property(
        isolate, inst, "suffixes", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto suffixes = data->Suffixes();
            auto arr = v8::Array::New(isolate, 0);
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by suffixes.size()
            for (uint32_t i = 0; i < suffixes.size(); ++i) {
                if (suffixes[i].has_value()) {
                    arr->Set(context, i, v8_convert::ToV8(isolate, *suffixes[i])).Check();
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(arr);
        });

    // prefixnums - Sparse array of prefix IDs indexed by slot (3 max).
    // Reference parity (JSUnit.cpp:353-367): zero entries become JS undefined.
    /// @description Sparse array of the item's magic prefix IDs by slot (up to 3); Item units only.
    /// @type {number[]}
    Property(
        isolate, inst, "prefixnums", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto nums = data->PrefixNums();
            auto arr = v8::Array::New(isolate, 0);
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by nums.size()
            for (uint32_t i = 0; i < nums.size(); ++i) {
                if (nums[i] != 0) {
                    arr->Set(context, i, v8_convert::ToV8(isolate, nums[i])).Check();
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(arr);
        });

    // suffixnums - Sparse array of suffix IDs indexed by slot (3 max).
    // See `prefixnums` above for sparse-hole semantics.
    /// @description Sparse array of the item's magic suffix IDs by slot (up to 3); Item units only.
    /// @type {number[]}
    Property(
        isolate, inst, "suffixnums", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto nums = data->SuffixNums();
            auto arr = v8::Array::New(isolate, 0);
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by nums.size()
            for (uint32_t i = 0; i < nums.size(); ++i) {
                if (nums[i] != 0) {
                    arr->Set(context, i, v8_convert::ToV8(isolate, nums[i])).Check();
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(arr);
        });

    /// @description Full multi-line display name of the item (quality, runeword, sockets, etc., newline-separated);
    /// Item units only.
    /// @type {string}
    Property(
        isolate, inst, "fname", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->ItemFullName()));
        });

    /// @description Item quality; Item units only.
    /// @type {ItemQuality}
    Property(
        isolate, inst, "quality", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->Quality()));
        });

    /// @description Inventory node/page the item belongs to; Item units only.
    /// @type {NodePage}
    Property(
        isolate, inst, "node", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->Node()));
        });

    /// @description Storage location of the item; Item units only.
    /// @type {ItemLocation}
    Property(
        isolate, inst, "location", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint8_t>(data->ItemLocation()));
        });

    /// @description Item width in inventory grid cells (Item units only).
    /// @type {number}
    Property(
        isolate, inst, "sizex", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->Size().width);
        });

    /// @description Item height in inventory grid cells (Item units only).
    /// @type {number}
    Property(
        isolate, inst, "sizey", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->Size().height);
        });

    /// @description Item type code from itemtypes.txt (sword, helm, ring, etc.; ItemType enum); Item units only.
    /// @type {number}
    Property(
        isolate, inst, "itemType", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->ItemType());
        });

    /// @description Full item tooltip description text (stat lines, newline-separated); Item units only.
    /// @type {string}
    Property(
        isolate, inst, "description", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = info.GetIsolate();
            auto lock = game::Bridge::Lock();
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->Description()));
        });

    /// @description Equipment slot (body location) the item is worn in; Item units only.
    /// @type {BodyLocation}
    Property(
        isolate, inst, "bodylocation", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->BodyLocation()));
        });

    /// @description Item level (the ilvl used for affix/drop calculations); Item units only.
    /// @type {number}
    Property(
        isolate, inst, "ilvl", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->ItemLevel());
        });

    /// @description Character level required to equip/use the item (includes affix-based increases); Item units only.
    /// @type {number}
    Property(
        isolate, inst, "lvlreq", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->LevelRequirement());
        });

    /// @description Alternate graphic / inventory-image index of the item (the "transform" gfx variant); Item units
    /// only.
    /// @type {number}
    Property(
        isolate, inst, "gfx", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->GfxIndex());
        });

    // =========================================================================
    // Player-specific properties (read-only on unit template; me object overrides with writable version)
    // =========================================================================

    /// @description Current movement mode of the player; player unit only.
    /// @type {MoveMode}
    Property(
        isolate, inst, "runwalk", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            // Only return a value for the player unit (matches reference: pUnit == GetPlayerUnit)
            auto player = game::Unit::Player();
            if (player && *data == player) {
                info.GetReturnValue().Set(data->RunWalk());
            }
        });

    /// @description Active weapon set of the player; player unit only.
    /// @type {WeaponSet}
    Property(
        isolate, inst, "weaponswitch", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data) {
                return;
            }
            // Only return a value for the player unit (matches reference: pUnit == GetPlayerUnit)
            auto player = game::Unit::Player();
            if (player && *data == player) {
                info.GetReturnValue().Set(data->WeaponSwitch());
            }
        });

    // =========================================================================
    // Object-specific properties
    // =========================================================================

    // Reference lines 503-511: only Objects have pObjectData; other types return undefined.
    /// @description Object subtype/category code from objects.txt (chest, shrine, waypoint, portal, etc.); Object units
    /// only.
    /// @type {number}
    Property(
        isolate, inst, "objtype", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Object) {
                return;
            }
            info.GetReturnValue().Set(data->ObjType());
        });

    // Reference lines 512-515: only Objects have pObjectData; other types return undefined.
    /// @description Whether the object is locked (e.g. a locked chest); Object units only.
    /// @type {boolean}
    Property(
        isolate, inst, "islocked", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* data = Unwrap(info.Holder());
            if (!*data || data->Type() != UnitType::Object) {
                return;
            }
            info.GetReturnValue().Set(data->IsLocked());
        });

    // =========================================================================
    // Methods
    // =========================================================================

    // Reference: unit_getNext does NOT call WaitForGameReady
    /// @description Advances this handle in place to the next unit (or inventory item) matching the stored
    /// getUnit/getItem search criteria, optionally refining the cursor filters first.
    /// @signature getNext()
    /// @signature getNext(classId: number, mode?: number)
    /// @param classId {number} - Optional class-id filter (first arg when numeric). Pass -1 (or omit) for no filter.
    /// @param mode {number} - Optional unit-mode filter (second arg). Three forms: (1) a plain value matches units
    /// whose mode equals it; (2) for Item units, a value >= 100 filters by item location instead, matching items whose
    /// location == mode-100 (100=ground, 101=equipped, 102=belt, 103=inventory, 104=store, 105=trade, 106=cube,
    /// 107=stash); (3) setting bit 29 (mode | 0x20000000) turns the low bits into a bitmask, matching any unit whose
    /// mode equals one of the bit positions 0..27 set in mode. Pass -1 (or omit) for no filter.
    /// @signature getNext(name: string, mode?: number)
    /// @param name {string} - Optional name filter (first arg when a string).
    /// @returns {boolean} - True if a next match was found and the handle now points at it; false otherwise.
    Method(
        isolate, proto, "getNext", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // IsUint32 rejects -1 ("no filter") while IsNumber would coerce it to 0xFFFFFFFF.
            if (args.Length() > 0 && args[0]->IsString()) {
                data->Cursor().name = v8_convert::ToString(isolate, args[0]);
            }
            if (args.Length() > 0 && args[0]->IsUint32()) {
                data->Cursor().classId = v8_convert::ToUint32(isolate, args[0]);
            }
            if (args.Length() > 1 && args[1]->IsUint32()) {
                data->Cursor().mode = v8_convert::ToUint32(isolate, args[1]);
            }

            if (data->Kind() == game::UnitKind::InventoryItem) {
                auto next = data->FindNextInventoryItem();
                if (!next) {
                    args.GetReturnValue().SetFalse();
                } else {
                    *data = *next;
                    args.GetReturnValue().Set(true);
                }
            } else {
                auto next = data->FindNext();
                if (!next) {
                    args.GetReturnValue().SetFalse();
                } else {
                    *data = *next;
                    args.GetReturnValue().Set(true);
                }
            }
        });

    // Reference: unit_cancel does NOT check unit validity - cancel is a global action
    /// @description Closes the current interaction / clears the cursor (a global UI action; the unit it is called on is
    /// ignored).
    /// @signature cancel(mode?: number)
    /// @param mode {number} - Optional CancelMode (0=close interact, 1=clear cursor, 2=close NPC, 3=clear screen);
    /// auto-detected when omitted.
    /// @returns {boolean} - True once the cancel was issued; false if the game was not ready.
    Method(
        isolate, proto, "cancel", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto mode = static_cast<game::CancelMode>(-1);  // -1 = auto-detect
            if (args.Length() > 0 && args[0]->IsNumber()) {
                mode = static_cast<game::CancelMode>(v8_convert::ToInt32(isolate, args[0]));
            }
            d2bs::game::Cancel(mode);
            args.GetReturnValue().Set(true);
        });

    /// @description Sends the "repair all" request to the currently open NPC repair menu (this unit is the NPC
    /// context).
    /// @signature repair()
    /// @returns {boolean} - True once the repair request was sent; false if the unit handle is invalid.
    Method(
        isolate, proto, "repair", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            data->Repair();
            args.GetReturnValue().Set(true);
        });

    /// @description Selects an entry in the currently open NPC menu for this NPC unit by menu id.
    /// @signature useMenu(menuId: number)
    /// @param menuId {number} - Required uint32 NPC menu entry id.
    /// @returns {boolean} - True if the menu entry was selected; false on failure.
    Method(
        isolate, proto, "useMenu", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* isolate = args.GetIsolate();
            uint32_t menuId = v8_convert::ToUint32(isolate, args[0]);
            args.GetReturnValue().Set(data->UseMenu(menuId));
        });

    // Reference: returns early if unit is the player unit (can't interact with self).
    // For items not on ground, game::Unit::Interact() must handle inventory/stash items
    // (packet 0x20) and belt items (packet 0x26) based on GetItemLocation().
    /// @description Interacts with the unit (talk to NPC, open object, pick up/move item, move toward monster, etc.)
    /// using the packet appropriate for the unit type and item location.
    /// @signature interact()
    /// @signature interact(waypointId: number)
    /// @param waypointId {number} - Optional uint32 waypoint id; honored only when the unit is an Object and exactly
    /// one numeric arg is passed (takes that waypoint instead of a plain interact).
    /// @returns {boolean} - True if the interact/waypoint action succeeded; false if the game was not ready, the unit
    /// is the player, or the action failed.
    Method(
        isolate, proto, "interact", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            args.GetReturnValue().SetFalse();
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 829: if unit is the player unit, return early
            auto player = game::Unit::Player();
            if (player && *data == player) {
                return;
            }
            // Reference line 854: waypoint path requires UNIT_OBJECT and exactly 1 argument
            if (data->Type() == UnitType::Object && args.Length() == 1 && args[0]->IsNumber()) {
                auto* isolate = args.GetIsolate();
                uint32_t waypointId = v8_convert::ToUint32(isolate, args[0]);
                args.GetReturnValue().Set(data->TakeWaypoint(waypointId));
            } else {
                args.GetReturnValue().Set(data->Interact());
            }
        });

    /// @description Finds the first item in this unit's inventory matching the optional filters and returns it as a new
    /// Unit handle (an InventoryItem cursor, so getNext continues the inventory walk).
    /// @signature getItem()
    /// @signature getItem(classId: number, mode?: number, unitId?: number)
    /// @param classId {number} - Optional item class-id filter (first arg when numeric). Pass -1 (or omit) for no
    /// filter.
    /// @param mode {number} - Optional item-mode filter (second arg). Three forms: (1) a plain value matches items
    /// whose mode equals it; (2) a value >= 100 filters by item location instead, matching items whose location ==
    /// mode-100 (100=ground, 101=equipped, 102=belt, 103=inventory, 104=store, 105=trade, 106=cube, 107=stash); (3)
    /// setting bit 29 (mode | 0x20000000) turns the low bits into a bitmask, matching any item whose mode equals one of
    /// the bit positions 0..27 set in mode. Pass -1 (or omit) for no filter.
    /// @param unitId {number} - Optional item unit-id filter (third arg). Pass -1 (or omit) for no filter.
    /// @signature getItem(name: string, mode?: number, unitId?: number)
    /// @param name {string} - Optional item name filter (first arg when a string).
    /// @returns {Unit} - The matching inventory item as a Unit, undefined if none, false if the game was not ready.
    Method(
        isolate, proto, "getItem", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }

            game::UnitCursorState cursor;
            if (args.Length() > 0 && args[0]->IsString()) {
                cursor.name = v8_convert::ToString(isolate, args[0]);
            } else if (args.Length() > 0 && args[0]->IsUint32()) {
                cursor.classId = v8_convert::ToUint32(isolate, args[0]);
            }
            if (args.Length() > 1 && args[1]->IsUint32()) {
                cursor.mode = v8_convert::ToUint32(isolate, args[1]);
            }
            if (args.Length() > 2 && args[2]->IsUint32()) {
                cursor.unitId = v8_convert::ToUint32(isolate, args[2]);
            }

            auto lock = game::Bridge::Lock();
            auto invItem = data->FindFirstInventoryItem(cursor);
            if (invItem) {
                auto context = isolate->GetCurrentContext();
                auto result = CreateInstance(isolate, context, std::make_unique<game::Unit>(*invItem));
                args.GetReturnValue().Set(result);
            }
        });

    /// @description Returns all items in this unit's inventory as Unit handles.
    /// @signature getItems()
    /// @returns {Unit[]} - Array of inventory items as Unit objects, undefined if empty, false if the game was not
    /// ready.
    Method(
        isolate, proto, "getItems", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }

            auto lock = game::Bridge::Lock();
            // Reference parity: GetItems() returns Regular kind (not InventoryItem).
            auto items = data->GetItems();
            if (items.empty()) {
                return;
            }
            auto context = isolate->GetCurrentContext();
            auto arr = v8::Array::New(isolate, static_cast<int32_t>(items.size()));
            for (uint32_t i = 0; i < items.size(); ++i) {
                auto& item = items.at(i);
                auto obj = CreateInstance(isolate, context, std::make_unique<game::Unit>(item));
                if (obj.IsEmpty()) {
                    v8_error::ThrowError(isolate, "Failed to build item array");
                    return;
                }
                arr->Set(context, i, obj).Check();
            }
            args.GetReturnValue().Set(arr);
        });

    /// @description Queries the unit's skills, either by mode selector (one arg) or by looking up a specific skill's
    /// level (multiple args).
    /// @signature getSkill(mode: number)
    /// @param mode {number} - Selector: 0/1 = right/left hand skill name, 2/3 = right/left hand skill id, 4 = array of
    /// [skillId, baseLevel, totalLevel] for all skills.
    /// @signature getSkill(skillId: number, includeBonus: number, chargeOnly?: boolean)
    /// @param skillId {number} - The skill id to look up the level for.
    /// @param includeBonus {number} - Include extra/bonus skill levels when truthy (must be number-typed to select this
    /// form).
    /// @param chargeOnly {boolean} - Optional: true restricts to charge skills, false counts non-charge, omitted counts
    /// both.
    /// @returns {string|number|Array<number[]>|boolean} - Skill name (modes 0/1), skill id (modes 2/3), all-skills
    /// array (mode 4), the skill level (multi-arg form), or false when not found / invalid mode.
    Method(
        isolate, proto, "getSkill", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() == 0 || args.Length() > 3) {
                return;
            }

            // Reference: all args must be int-typed; early-return if args[0] is not a number
            if (!args[0]->IsNumber()) {
                return;
            }

            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint16_t nSkillId = static_cast<uint16_t>(v8_convert::ToUint32(isolate, args[0]));

            if (args.Length() == 1) {
                // Mode switch: 0=right skill name, 1=left skill name, 2=right skill id,
                //              3=left skill id, 4=all skills array
                switch (nSkillId) {
                    case 0:
                    case 1:
                        args.GetReturnValue().Set(v8_convert::ToV8(
                            isolate, data->GetSkillName(nSkillId > 0 ? game::Hand::Left : game::Hand::Right)));
                        break;
                    case 2:
                    case 3:
                        args.GetReturnValue().Set(static_cast<uint32_t>(
                            data->GetSkillId((nSkillId - 2) > 0 ? game::Hand::Left : game::Hand::Right)));
                        break;
                    case 4: {
                        auto skills = data->GetAllSkills();
                        auto context = isolate->GetCurrentContext();
                        auto arr = v8::Array::New(isolate, static_cast<int32_t>(skills.size()));
                        for (uint32_t i = 0; i < skills.size(); ++i) {
                            auto& skill = skills[i];
                            auto skillArr = v8::Array::New(isolate, 3);
                            skillArr->Set(context, 0, v8_convert::ToV8(isolate, skill.skillId)).Check();
                            skillArr->Set(context, 1, v8_convert::ToV8(isolate, skill.baseLevel)).Check();
                            skillArr->Set(context, 2, v8_convert::ToV8(isolate, skill.totalLevel)).Check();
                            arr->Set(context, i, skillArr).Check();
                        }
                        args.GetReturnValue().Set(arr);
                        break;
                    }
                    default:
                        args.GetReturnValue().SetFalse();
                        break;
                }
            } else if (args[1]->IsNumber()) {
                bool includeExtraLevels = v8_convert::ToBool(isolate, args[1]);
                std::optional charge = false;
                if (args.Length() >= 3) {
                    if (v8_convert::ToBool(isolate, args[2])) {
                        charge = true;
                    } else {
                        charge = std::nullopt;
                    }
                }
                if (auto level = data->GetSkillLevel(nSkillId, includeExtraLevels, charge)) {
                    args.GetReturnValue().Set(level.value());
                } else {
                    args.GetReturnValue().SetFalse();
                }
            }
        });

    // Reference: returns different types based on unit type:
    //   Monster -> owner Unit (via GetMonsterOwner)
    //   Object  -> owner name STRING (pObjectData->szOwner)
    //   Item    -> owner Unit (from inventory)
    //   Missile -> owner Unit (via GetMissileOwnerUnit)
    /// @description Returns the unit's parent/owner, with a type that depends on this unit's type (owner name string
    /// for Objects; owning Unit for Monster/Item/Missile).
    /// @signature getParent()
    /// @returns {string|Unit|null} - Owner name string for Objects, owner Unit for Monster/Item/Missile, null if no
    /// owner, false if the game was not ready.
    Method(
        isolate, proto, "getParent", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            auto* isolate = args.GetIsolate();

            if (data->Type() == UnitType::Object) {
                auto ownerName = data->GetParentName();
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, ownerName));
                return;
            }

            auto owner = data->GetOwner();
            if (!owner) {
                args.GetReturnValue().SetNull();
                return;
            }
            auto context = isolate->GetCurrentContext();
            args.GetReturnValue().Set(CreateInstance(isolate, context, std::make_unique<game::Unit>(*owner)));
        });

    /// @description Returns this player's hired mercenary as a Unit (Player units only).
    /// @signature getMerc()
    /// @returns {Unit|null} - The mercenary Unit, null if none or not a player, false if the game was not ready.
    Method(
        isolate, proto, "getMerc", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            args.GetReturnValue().SetNull();
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 1668: only players have mercenaries
            if (data->Type() != UnitType::Player) {
                return;
            }
            auto merc = data->FindMerc();
            if (!merc) {
                return;
            }
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();
            args.GetReturnValue().Set(CreateInstance(isolate, context, std::make_unique<game::Unit>(merc.value())));
        });

    // Reference line 1697: lpUnit ? FindUnit(lpUnit) : GetPlayerUnit(), then GetMercUnit + HP%
    /// @description Returns the owning player's mercenary HP as a percentage (0-100) of its max HP (uses this unit if
    /// valid, else the local player).
    /// @signature getMercHP()
    /// @returns {number} - Mercenary HP percent 0-100 (0 if dead or no max HP), undefined if no merc, false if the game
    /// was not ready.
    Method(
        isolate, proto, "getMercHP", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            // Reference: lpUnit ? FindUnit(lpUnit) : GetPlayerUnit()
            auto* data = Unwrap(args.This());
            game::Unit unit;
            if (data && *data) {
                unit = *data;
            } else {
                unit = game::Unit::Player();
            }
            if (!unit)
                return;
            // Reference: pUnit->dwMode == 12 means dead, return 0
            if (unit.Mode() == 12) {
                args.GetReturnValue().Set(0);
                return;
            }
            auto merc = unit.FindMerc();
            if (!merc)
                return;
            uint32_t maxHp = merc->HpMax();
            args.GetReturnValue().Set(maxHp > 0 ? (100 * merc->Hp()) / maxHp : 0);
        });

    /// @description Checks whether this monster carries the given enchantment id (Monster units only).
    /// @signature getEnchant(enchantId: number)
    /// @param enchantId {number} - Required uint32 enchantment id to test for.
    /// @returns {boolean|number} - Boolean true if present, integer 0 if absent, undefined if not a monster, false if
    /// the game was not ready.
    Method(
        isolate, proto, "getEnchant", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 1914: only monsters have enchants
            if (data->Type() != UnitType::Monster) {
                return;
            }
            auto* isolate = args.GetIsolate();
            uint32_t enchantId = v8_convert::ToUint32(isolate, args[0]);
            // Returns INT 0 when not found, BOOLEAN true when found
            if (data->HasEnchant(enchantId)) {
                args.GetReturnValue().Set(true);
            } else {
                args.GetReturnValue().Set(0);
            }
        });

    // Reference line 1929-1941: no unit involved, uses global quest info directly.
    /// @description Returns the quest progress flag/state for the given quest and flag from the local player's quest
    /// data (the unit is ignored).
    /// @signature getQuest(quest: number, flag: number)
    /// @param quest {number} - Required uint32 quest number.
    /// @param flag {number} - Required uint32 quest flag.
    /// @returns {number} - Quest flag/state value; false if the game was not ready.
    Method(
        isolate, proto, "getQuest", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
                return;
            }
            auto* isolate = args.GetIsolate();
            uint32_t nQuest = v8_convert::ToUint32(isolate, args[0]);
            uint32_t nFlag = v8_convert::ToUint32(isolate, args[1]);
            args.GetReturnValue().Set(game::GetQuestFlag(nQuest, nFlag));
        });

    /// @description Tests whether the given status-effect state id is currently active on the unit.
    /// @signature getState(stateId: number)
    /// @param stateId {number} - Required non-negative state id (no upper bound, since mods may add states).
    /// @returns {boolean} - True if the state is active, false otherwise.
    Method(
        isolate, proto, "getState", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            int32_t nState = v8_convert::ToInt32(isolate, args[0]);
            // No max state check, as mods might add new stats.
            if (nState < 0) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            args.GetReturnValue().Set(data->HasState(static_cast<uint32_t>(nState)));
        });

    /// @description Reads unit stats: a single stat value, or one of two whole-table modes selected by a negative stat
    /// id.
    /// @signature getStat(statId: number, subIndex?: number)
    /// @param statId {number} - Stat id; -1 = flat array of [statId, subIndex, value] triples, -2 = sparse array
    /// indexed by statId with detailed charge/skill info, otherwise a specific stat id. Special case: statId 92 (item
    /// level requirement) returns the computed required level (after item/quality modifiers) rather than the raw stat.
    /// The experience stats are returned as an unsigned double to avoid int32 overflow.
    /// @param subIndex {number} - Optional stat sub-index (default 0); used for per-skill/per-element stats in the
    /// normal path.
    /// @returns {number|Array<any>|boolean} - The stat value (unsigned double for experience stats), an array for the
    /// -1 / -2 modes, false on bad args / invalid unit.
    Method(
        isolate, proto, "getStat", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            int32_t statId = v8_convert::ToInt32(isolate, args[0]);

            uint32_t subIndex = 0;
            if (args.Length() > 1) {
                subIndex = v8_convert::ToInt32(isolate, args[1]);
            }

            if (statId == -1) {
                // Return flat array of [statId, subIndex, value] sub-arrays
                // Reference: merges pUnit->pStats->StatVec with D2COMMON_GetStatList(pUnit, NULL, 0x40)
                auto allStats = data->GetAllStats();
                auto context = isolate->GetCurrentContext();
                auto arr = v8::Array::New(isolate, static_cast<int32_t>(allStats.size()));
                for (uint32_t i = 0; i < allStats.size(); ++i) {
                    auto& entry = allStats[i];
                    auto statArr = v8::Array::New(isolate, 3);
                    statArr->Set(context, 0, v8_convert::ToV8(isolate, entry.statId)).Check();
                    statArr->Set(context, 1, v8_convert::ToV8(isolate, entry.subIndex)).Check();
                    statArr->Set(context, 2, v8_convert::ToV8(isolate, entry.value)).Check();
                    arr->Set(context, i, statArr).Check();
                }
                args.GetReturnValue().Set(arr);
            } else if (statId == -2) {
                // Return sparse array indexed by statId with detailed charge info.
                // Reference: InsertStatsToGenericObject builds sparse array where:
                //   - For wSubIndex > 0x200: creates objects with {skill, level, charges, maxcharges}
                //     (skill = subIndex >> 6, level = subIndex & 0x3F, charges/maxcharges from value)
                //   - For normal stats: creates sub-arrays indexed by subIndex containing the value
                //   - Stats 6-11 (hp/mana/stamina) are right-shifted by 8
                // Delegates to game::Unit::GetDetailedStats() for the raw data.
                auto detailedStats = data->GetDetailedStats();
                auto context = isolate->GetCurrentContext();
                auto arr = v8::Array::New(isolate, 0);
                for (const auto& entry : detailedStats) {
                    v8::HandleScope innerScope(isolate);
                    if (entry.subIndex > 0x200) {
                        // Charge/skill stat: build object with {skill, level, charges, maxcharges}
                        int32_t skill = static_cast<int32_t>(entry.subIndex >> 6);
                        int32_t level = static_cast<int32_t>(entry.subIndex & 0x3F);
                        int32_t charges = 0;
                        int32_t maxcharges = 0;
                        if (entry.value > 0x200) {
                            charges = entry.value & 0xFF;
                            maxcharges = entry.value >> 8;
                        }
                        auto obj = v8::Object::New(isolate);
                        obj->Set(context, v8_convert::ToV8(isolate, "skill"), v8_convert::ToV8(isolate, skill)).Check();
                        obj->Set(context, v8_convert::ToV8(isolate, "level"), v8_convert::ToV8(isolate, level)).Check();
                        if (maxcharges > 0) {
                            obj->Set(context, v8_convert::ToV8(isolate, "charges"), v8_convert::ToV8(isolate, charges))
                                .Check();
                            obj->Set(context, v8_convert::ToV8(isolate, "maxcharges"),
                                     v8_convert::ToV8(isolate, maxcharges))
                                .Check();
                        }
                        // Place at arr[statId]; if already exists, wrap in array
                        v8::Local<v8::Value> existing;
                        if (arr->Get(context, entry.statId).ToLocal(&existing) && !existing->IsUndefined()) {
                            if (existing->IsArray()) {
                                auto existingArr = existing.As<v8::Array>();
                                existingArr->Set(context, existingArr->Length(), obj).Check();
                            } else {
                                auto newArr = v8::Array::New(isolate, 2);
                                newArr->Set(context, 0, existing).Check();
                                newArr->Set(context, 1, obj).Check();
                                arr->Set(context, entry.statId, newArr).Check();
                            }
                        } else {
                            arr->Set(context, entry.statId, obj).Check();
                        }
                    } else {
                        // Normal stat: value placed in sub-array at arr[statId][subIndex].
                        // GetDetailedStats already applies the 8.8 fixed-point shift to
                        // stats 6-11 (hp/mana/stamina) - getStat(-2) parity, no extra
                        // shift here. (getStat(-1) keeps raw values; see GetAllStats.)
                        int32_t value = entry.value;
                        v8::Local<v8::Value> existing;
                        if (!arr->Get(context, entry.statId).ToLocal(&existing) || existing->IsUndefined()) {
                            existing = v8::Array::New(isolate, 0);
                            arr->Set(context, entry.statId, existing).Check();
                        }
                        if (existing->IsArray()) {
                            existing.As<v8::Array>()
                                ->Set(context, entry.subIndex, v8_convert::ToV8(isolate, value))
                                .Check();
                        }
                    }
                }
                args.GetReturnValue().Set(arr);
            } else {
                // Reference lines 916-940: special cases for the normal (statId >= 0) path
                if (statId == static_cast<int32_t>(game::STAT_ITEMLEVELREQ)) {
                    // STAT_ITEMLEVELREQ: reference calls D2COMMON_GetItemLevelRequirement
                    args.GetReturnValue().Set(data->LevelRequirement());
                } else {
                    // GetStat handles the >>8 shift for stats 6-11 internally.
                    int32_t value = data->GetStat(static_cast<uint32_t>(statId), subIndex);
                    // Stats EXP, LASTEXP, NEXTEXP: return as unsigned double
                    // to handle large XP values that overflow int32_t.
                    // Reference line 918-921: JS_NumberValue((unsigned int)value)
                    if (statId == static_cast<int32_t>(game::STAT_EXP) ||
                        statId == static_cast<int32_t>(game::STAT_LASTEXP) ||
                        statId == static_cast<int32_t>(game::STAT_NEXTEXP)) {
                        args.GetReturnValue().Set(
                            v8_convert::ToV8(isolate, static_cast<double>(static_cast<uint32_t>(value))));
                        return;
                    }
                    args.GetReturnValue().Set(value);
                }
            }
        });

    /// @description Returns the item's full flags bitmask (identified, broken, socketed, ethereal, runeword, etc.);
    /// Item units only.
    /// @signature getFlags()
    /// @returns {number} - Item flags bitmask, undefined if not an item, false if the game was not ready.
    Method(
        isolate, proto, "getFlags", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 1141: only items have flags
            if (data->Type() != UnitType::Item) {
                return;
            }
            args.GetReturnValue().Set(data->ItemFlags());
        });

    /// @description Tests whether the given item flag bit(s) are set in the item's flags bitmask; Item units only.
    /// @signature getFlag(flag: number)
    /// @param flag {number} - Required uint32 flag bit mask to test.
    /// @returns {boolean} - True if any masked flag bit is set, undefined if not an item, false if the game was not
    /// ready.
    Method(
        isolate, proto, "getFlag", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 1163: only items have flags
            if (data->Type() != UnitType::Item) {
                return;
            }
            auto* isolate = args.GetIsolate();
            uint32_t flag = v8_convert::ToUint32(isolate, args[0]);
            args.GetReturnValue().Set((data->ItemFlags() & flag) > 0);
        });

    /// @description Computes the buy/sell/repair cost of this item at an NPC; Item units only.
    /// @signature getItemCost(mode: number, npc?: Unit, difficulty?: number)
    /// @param mode {number} - Required ItemCostMode: 0=buy, 1=sell, 2=repair (other values return undefined).
    /// @param npc {Unit} - Optional NPC as a Unit object (its classId is used); defaults to the interacting NPC, else
    /// Charsi.
    /// @param difficulty {number} - Optional difficulty value; defaults to the current game difficulty.
    /// @signature getItemCost(mode: number, npcClassId?: number, difficulty?: number)
    /// @param npcClassId {number} - Optional NPC class id (alternative to passing a Unit); defaults to the interacting
    /// NPC, else Charsi.
    /// @returns {number} - The item cost in gold, undefined if not an item or invalid mode, false if the game was not
    /// ready.
    Method(
        isolate, proto, "getItemCost", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 1243-1244: only items have cost
            if (data->Type() != UnitType::Item) {
                return;
            }
            auto mode = static_cast<game::ItemCostMode>(v8_convert::ToInt32(isolate, args[0]));
            // Reference line 1277-1287: only modes Buy(0), Sell(1), Repair(2) are valid
            if (mode < game::ItemCostMode::Buy || mode > game::ItemCostMode::Repair) {
                return;
            }
            // Default NPC: use currently interacting NPC, fall back to Charsi
            auto npc = game::Unit::InteractingNPC();
            uint32_t npcClassId = npc ? npc->ClassId() : game::NPC_CHARSI_CLASS_ID;
            auto difficulty = game::GetDifficulty();
            if (args.Length() > 1) {
                if (args[1]->IsObject()) {
                    // If NPC passed as a Unit object, unwrap and get its classId
                    auto* npcUnit = Unwrap(args[1].As<v8::Object>());
                    if (npcUnit && *npcUnit) {
                        npcClassId = npcUnit->ClassId();
                    }
                } else if (args[1]->IsNumber()) {
                    npcClassId = v8_convert::ToUint32(isolate, args[1]);
                }
            }
            if (args.Length() > 2 && args[2]->IsNumber()) {
                difficulty = static_cast<game::Difficulty>(v8_convert::ToInt32(isolate, args[2]));
            }
            args.GetReturnValue().Set(data->ItemCost(mode, npcClassId, difficulty));
        });

    /// @description Sets the player's active skill on the given hand and blocks until the game confirms the bind (up to
    /// ~1 second, pumping the script event queue).
    /// @signature setSkill(skill: string, hand: number, item?: Unit)
    /// @param skill {string} - Skill name, resolved via the skill name table.
    /// @param hand {number} - Required numeric: truthy = left hand, falsy = right hand.
    /// @param item {Unit} - Optional Item Unit to bind the skill from (e.g. an item-granted/charge skill); ignored if
    /// not an item.
    /// @signature setSkill(skillId: number, hand: number, item?: Unit)
    /// @param skillId {number} - Skill id (alternative to the name string).
    /// @returns {boolean} - True once the skill is confirmed bound; false on unresolved skill, non-numeric hand, or
    /// timeout.
    Method(
        isolate, proto, "setSkill", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint16_t skillId = 0;
            if (args[0]->IsString()) {
                std::string skillName = v8_convert::ToString(isolate, args[0]);
                auto resolved = game::GetSkillByName(skillName);
                if (!resolved.has_value()) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                skillId = resolved.value();
            } else if (args[0]->IsNumber()) {
                skillId = static_cast<uint16_t>(v8_convert::ToInt32(isolate, args[0]));
            } else {
                args.GetReturnValue().SetFalse();
                return;
            }
            // Reference requires arg[1] to be an int -- returns false if missing or wrong type
            if (args.Length() < 2 || !args[1]->IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            // JS arg is numeric 0/1 (truthy->leftHand). Preserve that at the binding boundary.
            game::Hand hand = v8_convert::ToBool(isolate, args[1]) ? game::Hand::Left : game::Hand::Right;
            std::optional<uint32_t> itemId;
            if (args.Length() == 3 && args[2]->IsObject()) {
                auto* itemUnit = Unwrap(args[2].As<v8::Object>());
                if (itemUnit && *itemUnit && itemUnit->Type() == UnitType::Item) {
                    itemId = itemUnit->Id();
                }
            }
            // Wait up to 1s for the skill to be bound. One loop handles three
            // states: already bound (GetSkillId match), validation fails (skill
            // not yet in pSkills - weapon-swap-in-progress window; pump events
            // and retry), validation passes (send packet once, keep polling).
            // ExecuteEvents drains the script event queue during the wait so
            // blocking game->script events get ack'd and don't stall the game
            // thread. Reference parity: D2Helpers.cpp:259-285.
            auto* script = ScriptEngine::Instance().GetScript(isolate);
            using namespace std::chrono_literals;
            const auto deadline = std::chrono::steady_clock::now() + 1s;
            bool packetSent = false;
            while (std::chrono::steady_clock::now() < deadline) {
                if (data->GetSkillId(hand) == skillId) {
                    args.GetReturnValue().Set(true);
                    return;
                }
                if (!packetSent && data->SetSkill(skillId, hand, itemId)) {
                    packetSent = true;
                }
                script->ExecuteEvents(20ms);
            }
            args.GetReturnValue().SetFalse();
        });

    /// @description Issues a single move/walk command toward a target world coordinate, or to this unit's own position
    /// when no coordinates are given.
    /// @signature move()
    /// @signature move(x: number, y: number)
    /// @param x {number} - Target X world coordinate (required together with y; required when called on the player
    /// unit).
    /// @param y {number} - Target Y world coordinate.
    /// @returns {undefined} - No return value; false only if the game was not ready.
    Method(
        isolate, proto, "move", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            auto player = game::Unit::Player();
            if (!player) {
                return;
            }
            // Reference line 1879-1881: if the unit IS the player, x,y args are required
            if (*data == player) {
                if (args.Length() < 2) {
                    return;
                }
            }
            // If called with x,y arguments, use those; otherwise move to the unit's position.
            // Explicit IsNumber gate preserves legacy behavior: non-numeric args (e.g. `move("foo","bar")`)
            // must be treated as "no target given" rather than coercing to 0.
            auto target = (args.Length() >= 2 && args[0]->IsNumber() && args[1]->IsNumber())
                              ? v8_extract::Position(args, 0).value_or(data->Pos())
                              : data->Pos();
            data->Move(target);
        });

    /// @description Displays an overhead chat/floating-text message above the unit.
    /// @signature overhead(text: string)
    /// @param text {string} - The message to show above the unit (coerced to string; empty string shows nothing).
    /// @returns {boolean} - True once handled; false if the game was not ready.
    Method(
        isolate, proto, "overhead", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            args.GetReturnValue().SetFalse();
            if (args.Length() > 0) {
                auto* data = Unwrap(args.This());
                if (!*data) {
                    return;
                }
                auto* isolate = args.GetIsolate();
                std::string text = v8_convert::ToString(isolate, args[0]);
                if (!text.empty()) {
                    data->Overhead(text);
                }
            }
            args.GetReturnValue().Set(true);
        });

    /// @description Sends the revive request for this unit (used to revive a dead hireling at an NPC).
    /// @signature revive()
    /// @returns {undefined} - No return value; false only if the game was not ready.
    Method(
        isolate, proto, "revive", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            data->Revive();
        });

    /// @description Buys or sells this item at the open NPC shop; Item units only.
    /// @signature shop(mode: number)
    /// @param mode {number} - ShopMode read from the last argument passed: 1=sell, 2=buy, 6=buy-fill (other values
    /// return false).
    /// @returns {boolean} - True if the shop action succeeded; false on invalid mode, not an item, or not ready.
    Method(
        isolate, proto, "shop", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            // Reference line 1486: shop only works on items
            if (data->Type() != UnitType::Item) {
                args.GetReturnValue().SetFalse();
                return;
            }
            // Reference reads mode from argv[argc-1] (last argument)
            int32_t lastIdx = args.Length() - 1;
            auto mode = static_cast<game::ShopMode>(v8_convert::ToInt32(isolate, args[lastIdx]));
            if (mode != game::ShopMode::Sell && mode != game::ShopMode::Buy && mode != game::ShopMode::BuyFill) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto lock = game::Bridge::Lock();
            args.GetReturnValue().Set(data->Shop(mode));
        });

    /// @description Returns the number of minions of the given type this unit owns/has summoned; Monster and Player
    /// units only.
    /// @signature getMinionCount(type: number)
    /// @param type {number} - Required minion type id to count.
    /// @returns {number} - Count of minions of that type, undefined if not a monster/player, false if the game was not
    /// ready.
    Method(
        isolate, proto, "getMinionCount", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0]->IsNumber()) {
                return;
            }
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            // Reference line 1960: only monsters and players have minions
            if (data->Type() != UnitType::Monster && data->Type() != UnitType::Player) {
                return;
            }
            auto* isolate = args.GetIsolate();
            int32_t nType = v8_convert::ToInt32(isolate, args[0]);
            args.GetReturnValue().Set(data->GetMinionCount(nType));
        });

    /// @description Returns the total gold cost to repair all of the local player's items at an NPC (always uses the
    /// player unit; the unit is ignored).
    /// @signature getRepairCost(npcClassId?: number)
    /// @param npcClassId {number} - Optional NPC class id; defaults to the interacting NPC, else Charsi.
    /// @returns {number} - Total repair cost in gold (0 if no player unit); false if the game was not ready.
    Method(
        isolate, proto, "getRepairCost", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                v8_error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            // Reference line 1978: always uses D2CLIENT_GetPlayerUnit(), not `this`
            auto player = game::Unit::Player();
            if (!player) {
                args.GetReturnValue().Set(0);
                return;
            }
            // Default NPC: use currently interacting NPC, fall back to Charsi
            auto npc = game::Unit::InteractingNPC();
            int32_t npcClassId =
                npc ? static_cast<int32_t>(npc->ClassId()) : static_cast<int32_t>(game::NPC_CHARSI_CLASS_ID);
            if (args.Length() > 0 && args[0]->IsNumber()) {
                npcClassId = v8_convert::ToInt32(isolate, args[0]);
            }
            args.GetReturnValue().Set(player.GetRepairCost(npcClassId));
        });

    // Symbol.iterator - yields [x, y] so `let [x, y] = unit;` works.
    // Snapshots Pos() at iterator-creation; delegates iteration state to
    // v8::Array's built-in iterator.
    auto iteratorTpl = v8::FunctionTemplate::New(
        isolate, +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto* data = Unwrap(args.This());
            if (!*data) {
                return;
            }
            auto pos = data->Pos();

            std::array<v8::Local<v8::Value>, 2> elements = {
                v8_convert::ToV8(isolate, pos.x),
                v8_convert::ToV8(isolate, pos.y),
            };
            auto arr = v8::Array::New(isolate, elements.data(), elements.size());

            auto iterMethod = arr->Get(context, v8::Symbol::GetIterator(isolate)).ToLocalChecked().As<v8::Function>();
            auto iter = iterMethod->Call(context, arr, 0, nullptr).ToLocalChecked();
            args.GetReturnValue().Set(iter);
        });
    proto->Set(v8::Symbol::GetIterator(isolate), iteratorTpl, v8::DontEnum);
}

}  // namespace d2bs::api::classes
