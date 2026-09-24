#include "JSUnit.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include "Receiver.h"
#include "api/classes/game/JSStashTab.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/core/Extract.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "config/AppConfig.h"
#include "game/Bridge.h"
#include "game/Constants.h"
#include "game/Finders.h"
#include "game/GameHelpers.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

using game::UnitType;

// NOLINTNEXTLINE(readability-function-size) - class configuration, intentionally large
void JSUnit::Configure(const ub::Class<Native>& cls) {
    /// @description Unit type category.
    /// @type {UnitType}
    Property(
        cls, "type", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr) {
                return;
            }
            // No *data check as type is cached
            info.GetReturnValue().Set(static_cast<uint32_t>(data->Type()));
        });

    /// @description Class ID of the unit (monster class, item class, object class, etc.).
    /// @type {number}
    Property(
        cls, "classid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->ClassId());
        });

    /// @description Current animation/action mode of the unit (walking, attacking, dead, etc.).
    /// @type {number}
    Property(
        cls, "mode", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Mode());
        });

    /// @description Display name of the unit (player name, monster name, item name, etc.).
    /// @type {string}
    Property(
        cls, "name", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            std::ignore = info.GetReturnValue().Set(data->Name());
        });

    /// @description Act the unit is currently in (1-5).
    /// @type {number}
    Property(
        cls, "act", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Act());
        });

    /// @description Extended unit flag bitmask (dwFlagEx); carries UNITFLAGEX_ISEXPANSION (0x2000000) per unit.
    /// @type {number}
    Property(
        cls, "flagsex", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->FlagsEx());
        });

    /// @description Global unit ID.
    /// @type {number}
    Property(
        cls, "gid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr) {
                return;
            }
            // No *data check as unit id is usually cached
            info.GetReturnValue().Set(static_cast<double>(data->Id()));
        });

    /// @description Unit X position in world subtile coordinates.
    /// @type {number}
    Property(
        cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Pos().x);
        });

    /// @description Unit Y position in world subtile coordinates.
    /// @type {number}
    Property(
        cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Pos().y);
        });

    // Reference lines 239-246: only Player/Monster/Missile have pPath->xTarget; other types return undefined.
    /// @description X position of the unit's movement/path target (Player, Monster, and Missile units only).
    /// @type {number}
    Property(
        cls, "targetx", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
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
        cls, "targety", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
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
        cls, "area", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Area());
        });

    /// @description Current hit points of the unit (whole-number HP, fixed-point shift already applied).
    /// @type {number}
    Property(
        cls, "hp", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Hp());
        });

    /// @description Maximum hit points of the unit (whole-number HP, fixed-point shift already applied).
    /// @type {number}
    Property(
        cls, "hpmax", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->HpMax());
        });

    /// @description Current mana points of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        cls, "mp", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Mp());
        });

    /// @description Maximum mana points of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        cls, "mpmax", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->MpMax());
        });

    /// @description Current stamina of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        cls, "stamina", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Stamina());
        });

    /// @description Maximum stamina of the unit (whole-number, fixed-point shift already applied).
    /// @type {number}
    Property(
        cls, "staminamax", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->StaminaMax());
        });

    /// @description Character/unit level.
    /// @type {number}
    Property(
        cls, "charlvl", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->CharLevel());
        });

    /// @description Number of items the unit owns/carries in its inventory list.
    /// @type {number}
    Property(
        cls, "itemcount", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->ItemCount());
        });

    // Emitted as int32_t so the "no owner" sentinel reaches JS as literal -1 rather than 0xFFFFFFFF.
    /// @description Owner unit ID for minions, missiles, and items (-1 when there is no owner).
    /// @type {number}
    Property(
        cls, "owner", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            auto owner = data->GetOwner();
            int32_t value = owner ? static_cast<int32_t>(owner->Id()) : -1;
            info.GetReturnValue().Set(value);
        });

    // Emitted as int32_t so the "no owner" sentinel reaches JS as literal -1.
    /// @description Unit type of this unit's owner (-1 when there is no owner).
    /// @type {number}
    Property(
        cls, "ownertype", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            auto owner = data->GetOwner();
            int32_t value = owner ? static_cast<int32_t>(owner->Type()) : -1;
            info.GetReturnValue().Set(value);
        });

    // Reference lines 280-297: only Monsters have pMonsterData; other types return undefined.
    /// @description Monster special-type bitflags (a bitfield combination); Monster units only.
    /// @type {MonsterSpecType}
    Property(
        cls, "spectype", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Monster) {
                return;
            }
            info.GetReturnValue().Set(data->SpecType());
        });

    /// @description Facing direction of the unit (game angle index, 0-63).
    /// @type {number}
    Property(
        cls, "direction", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(data->Direction());
        });

    /// @description Super-unique monster ID (SuperUniques.txt index). -1 for any other unit, including monsters that
    /// are not super-unique. Items carry their dwFileIndex on `fileindex` instead.
    /// @type {number}
    Property(
        cls, "uniqueid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
                return;
            }
            info.GetReturnValue().Set(static_cast<int32_t>(data->SuperUniqueId().value_or(-1)));
        });

    /// @description Three/four-character item code (e.g. "rvl", "gcv"); Item units only, "Unknown" if unresolvable.
    /// @type {string}
    Property(
        cls, "code", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto code = data->ItemCode();
            if (code.empty()) {
                code = "Unknown";
            }
            std::ignore = info.GetReturnValue().Set(code);
        });

    /// @description Localized name of the item's primary magic prefix (Item units only; empty if none).
    /// @type {string}
    Property(
        cls, "prefix", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            std::ignore = info.GetReturnValue().Set(data->Prefix());
        });

    /// @description Localized name of the item's primary magic suffix (Item units only; empty if none).
    /// @type {string}
    Property(
        cls, "suffix", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            std::ignore = info.GetReturnValue().Set(data->Suffix());
        });

    /// @description Numeric ID of the item's primary magic prefix (Item units only; 0 if none).
    /// @type {number}
    Property(
        cls, "prefixnum", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->PrefixNum()));
        });

    /// @description Numeric ID of the item's primary magic suffix (Item units only; 0 if none).
    /// @type {number}
    Property(
        cls, "suffixnum", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->SuffixNum()));
        });

    /// @description Numeric ID of the item's rare prefix, 1-based into the concatenated
    /// [raresuffix][rareprefix] table (Item units only; 0 if none).
    /// @type {number}
    Property(
        cls, "rareprefixnum", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->RarePrefixNum());
        });

    /// @description Numeric ID of the item's rare suffix, 1-based into the concatenated
    /// [raresuffix][rareprefix] table (Item units only; 0 if none).
    /// @type {number}
    Property(
        cls, "raresuffixnum", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->RareSuffixNum());
        });

    // prefixes - Sparse array of prefix names indexed by slot (3 max).
    // Reference parity (JSUnit.cpp:337-352): unset slots are JS undefined, set
    // slots carry the magic-mod string. `.length` reflects the highest set
    // index, mirroring SpiderMonkey's JS_SetElement behavior.
    /// @description Sparse array of the item's magic prefix names by slot (up to 3); Item units only.
    /// @type {string[]}
    Property(
        cls, "prefixes", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto& isolate = info.GetIsolate();
            const auto& context = info.GetContext();
            auto prefixes = data->Prefixes();
            // Reference (JSUnit.cpp:337-352) starts with an empty array and
            // only `JS_SetElement`s non-empty slots, so `.length` reflects
            // (highest-set-index + 1). Match that here.
            auto arr = ub::Array::New(context);
            if (!arr) {
                return;
            }
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by prefixes.size()
            for (uint32_t i = 0; i < prefixes.size(); ++i) {
                if (prefixes[i].has_value()) {
                    if (!arr->Set(context, i, convert::ToJS(isolate, *prefixes[i])).value_or(false)) {
                        return;
                    }
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(*arr);
        });

    // suffixes - Sparse array of suffix names indexed by slot (3 max).
    // See `prefixes` above for sparse-hole semantics.
    /// @description Sparse array of the item's magic suffix names by slot (up to 3); Item units only.
    /// @type {string[]}
    Property(
        cls, "suffixes", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto& isolate = info.GetIsolate();
            const auto& context = info.GetContext();
            auto suffixes = data->Suffixes();
            auto arr = ub::Array::New(context);
            if (!arr) {
                return;
            }
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by suffixes.size()
            for (uint32_t i = 0; i < suffixes.size(); ++i) {
                if (suffixes[i].has_value()) {
                    if (!arr->Set(context, i, convert::ToJS(isolate, *suffixes[i])).value_or(false)) {
                        return;
                    }
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(*arr);
        });

    // prefixnums - Sparse array of prefix IDs indexed by slot (3 max).
    // Reference parity (JSUnit.cpp:353-367): zero entries become JS undefined.
    /// @description Sparse array of the item's magic prefix IDs by slot (up to 3); Item units only.
    /// @type {number[]}
    Property(
        cls, "prefixnums", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto& isolate = info.GetIsolate();
            const auto& context = info.GetContext();
            auto nums = data->PrefixNums();
            auto arr = ub::Array::New(context);
            if (!arr) {
                return;
            }
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by nums.size()
            for (uint32_t i = 0; i < nums.size(); ++i) {
                if (nums[i] != 0) {
                    if (!arr->Set(context, i, convert::ToJS(isolate, nums[i])).value_or(false)) {
                        return;
                    }
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(*arr);
        });

    // suffixnums - Sparse array of suffix IDs indexed by slot (3 max).
    // See `prefixnums` above for sparse-hole semantics.
    /// @description Sparse array of the item's magic suffix IDs by slot (up to 3); Item units only.
    /// @type {number[]}
    Property(
        cls, "suffixnums", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto& isolate = info.GetIsolate();
            const auto& context = info.GetContext();
            auto nums = data->SuffixNums();
            auto arr = ub::Array::New(context);
            if (!arr) {
                return;
            }
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by nums.size()
            for (uint32_t i = 0; i < nums.size(); ++i) {
                if (nums[i] != 0) {
                    if (!arr->Set(context, i, convert::ToJS(isolate, nums[i])).value_or(false)) {
                        return;
                    }
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
            info.GetReturnValue().Set(*arr);
        });

    /// @description Full multi-line display name of the item (quality, runeword, sockets, etc., newline-separated);
    /// Item units only.
    /// @type {string}
    Property(
        cls, "fname", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            std::ignore = info.GetReturnValue().Set(data->ItemFullName());
        });

    /// @description Item quality; Item units only.
    /// @type {ItemQuality}
    Property(
        cls, "quality", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->Quality()));
        });

    /// @description Inventory node/page the item belongs to; Item units only.
    /// @type {NodePage}
    Property(
        cls, "node", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->Node()));
        });

    /// @description Storage location of the item; Item units only.
    /// @type {ItemLocation}
    Property(
        cls, "location", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto lock = game::Bridge::Lock();
            info.GetReturnValue().Set(static_cast<uint8_t>(data->ItemLocation()));
        });

    /// @description The stash tab holding this item; undefined unless the item is in your stash. Item units only.
    /// @type {StashTab|undefined}
    Property(
        cls, "stashTab", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto lock = game::Bridge::Lock();
            const auto tab = data->StashTab();
            if (!tab) {
                return;
            }
            if (auto obj = JSStashTab::Wrap(info.GetContext(), std::make_shared<game::StashTab>(*tab))) {
                info.GetReturnValue().Set(*obj);
            }
        });

    /// @description Item width in inventory grid cells (Item units only).
    /// @type {number}
    Property(
        cls, "sizex", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->Size().width);
        });

    /// @description Item height in inventory grid cells (Item units only).
    /// @type {number}
    Property(
        cls, "sizey", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->Size().height);
        });

    /// @description Item type code from itemtypes.txt (sword, helm, ring, etc.; ItemType enum); Item units only.
    /// @type {number}
    Property(
        cls, "itemType", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->ItemType());
        });

    /// @description Full item tooltip description text (stat lines, newline-separated); Item units only.
    /// @type {string}
    Property(
        cls, "description", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            auto lock = game::Bridge::Lock();
            std::ignore = info.GetReturnValue().Set(data->Description());
        });

    /// @description Equipment slot (body location) the item is worn in; Item units only.
    /// @type {BodyLocation}
    Property(
        cls, "bodylocation", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<uint32_t>(data->BodyLocation()));
        });

    /// @description Item level (the ilvl used for affix/drop calculations); Item units only.
    /// @type {number}
    Property(
        cls, "ilvl", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->ItemLevel());
        });

    /// @description Character level required to equip/use the item (includes affix-based increases); Item units only.
    /// @type {number}
    Property(
        cls, "lvlreq", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->LevelRequirement());
        });

    /// @description Alternate graphic / inventory-image index of the item (the "transform" gfx variant); Item units
    /// only.
    /// @type {number}
    Property(
        cls, "gfx", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->GfxIndex());
        });

    /// @description The item's dwFileIndex (-1 when none). The table it indexes depends on quality: a
    /// lowqualityitems, uniqueitems or setitems row, or for a normal item an elixir attribute, a monstats row for a
    /// body part, or an ear's character class. Item units only.
    /// @type {number}
    Property(
        cls, "fileindex", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(static_cast<int32_t>(data->FileIndex().value_or(-1)));
        });

    /// @description Item storage format (wItemFormat); 0 is a classic (pre-expansion) item. Item units only.
    /// @type {number}
    Property(
        cls, "itemformat", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->ItemFormat());
        });

    /// @description Level of the character an ear came from; 0 for anything else. Item units only.
    /// @type {number}
    Property(
        cls, "earlvl", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            info.GetReturnValue().Set(data->EarLevel());
        });

    /// @description Name stamped into a personalised item or an ear; empty otherwise. Item units only.
    /// @type {string}
    Property(
        cls, "playername", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Item) {
                return;
            }
            std::ignore = info.GetReturnValue().Set(data->ItemPlayerName());
        });

    // =========================================================================
    // Player-specific properties (read-only on unit template; me object overrides with writable version)
    // =========================================================================

    /// @description Current movement mode of the player; player unit only.
    /// @type {MoveMode}
    Property(
        cls, "runwalk", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
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
        cls, "weaponswitch", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data) {
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
        cls, "objtype", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Object) {
                return;
            }
            info.GetReturnValue().Set(data->ObjType());
        });

    // Reference lines 512-515: only Objects have pObjectData; other types return undefined.
    /// @description Whether the object is locked (e.g. a locked chest); Object units only.
    /// @type {boolean}
    Property(
        cls, "islocked", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* data = Receiver<JSUnit>(info);
            if (data == nullptr || !*data || data->Type() != UnitType::Object) {
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
        cls, "getNext", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            // IsUint32 rejects -1 ("no filter") while IsNumber would coerce it to 0xFFFFFFFF.
            if (args.Length() > 0 && args[0].IsString()) {
                data->Cursor().name = convert::ToString(context, args[0]);
            }
            if (args.Length() > 0 && args[0].IsUint32()) {
                data->Cursor().classId = convert::ToUint32(context, args[0]);
            }
            if (args.Length() > 1 && args[1].IsUint32()) {
                data->Cursor().mode = convert::ToUint32(context, args[1]);
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
        cls, "cancel", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto mode = static_cast<game::CancelMode>(-1);  // -1 = auto-detect
            if (args.Length() > 0 && args[0].IsNumber()) {
                mode = static_cast<game::CancelMode>(convert::ToInt32(context, args[0]));
            }
            d2bs::game::Cancel(mode);
            args.GetReturnValue().Set(true);
        });

    /// @description Sends the "repair all" request to the currently open NPC repair menu (this unit is the NPC
    /// context).
    /// @signature repair()
    /// @returns {boolean} - True once the repair request was sent; false if the unit handle is invalid.
    Method(
        cls, "repair", +[](const ub::CallbackInfo& args) {
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
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
        cls, "useMenu", +[](const ub::CallbackInfo& args) {
            if (args.Length() < 1 || !args[0].IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }
            const auto& context = args.GetContext();
            uint32_t menuId = convert::ToUint32(context, args[0]);
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
        cls, "interact", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            args.GetReturnValue().SetFalse();
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }
            // Reference line 829: if unit is the player unit, return early
            auto player = game::Unit::Player();
            if (player && *data == player) {
                return;
            }
            // Reference line 854: waypoint path requires UNIT_OBJECT and exactly 1 argument
            if (data->Type() == UnitType::Object && args.Length() == 1 && args[0].IsNumber()) {
                const auto& context = args.GetContext();
                uint32_t waypointId = convert::ToUint32(context, args[0]);
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
        cls, "getItem", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }

            game::UnitCursorState cursor;
            if (args.Length() > 0 && args[0].IsString()) {
                cursor.name = convert::ToString(context, args[0]);
            } else if (args.Length() > 0 && args[0].IsUint32()) {
                cursor.classId = convert::ToUint32(context, args[0]);
            }
            if (args.Length() > 1 && args[1].IsUint32()) {
                cursor.mode = convert::ToUint32(context, args[1]);
            }
            if (args.Length() > 2 && args[2].IsUint32()) {
                cursor.unitId = convert::ToUint32(context, args[2]);
            }

            auto lock = game::Bridge::Lock();
            auto invItem = data->FindFirstInventoryItem(cursor);
            if (invItem) {
                if (auto result = Wrap(context, std::make_shared<game::Unit>(*invItem))) {
                    args.GetReturnValue().Set(*result);
                }
            }
        });

    /// @description Returns all items in this unit's inventory as Unit handles.
    /// @signature getItems()
    /// @returns {Unit[]} - Array of inventory items as Unit objects, undefined if empty, false if the game was not
    /// ready.
    Method(
        cls, "getItems", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }

            auto lock = game::Bridge::Lock();
            // Reference parity: GetItems() returns Regular kind (not InventoryItem).
            auto items = data->GetItems();
            if (items.empty()) {
                return;
            }
            auto arr = ub::Array::New(context, static_cast<uint32_t>(items.size()));
            if (!arr) {
                return;
            }
            for (uint32_t i = 0; i < items.size(); ++i) {
                auto& item = items.at(i);
                auto obj = Wrap(context, std::make_shared<game::Unit>(item));
                if (!obj) {
                    error::ThrowError(isolate, "Failed to build item array");
                    return;
                }
                if (!arr->Set(context, i, *obj).value_or(false)) {
                    return;
                }
            }
            args.GetReturnValue().Set(*arr);
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
        cls, "getSkill", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() == 0 || args.Length() > 3) {
                return;
            }

            // Reference: all args must be int-typed; early-return if args[0] is not a number
            if (!args[0].IsNumber()) {
                return;
            }

            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint16_t nSkillId = static_cast<uint16_t>(convert::ToUint32(context, args[0]));

            if (args.Length() == 1) {
                // Mode switch: 0=right skill name, 1=left skill name, 2=right skill id,
                //              3=left skill id, 4=all skills array
                switch (nSkillId) {
                    case 0:
                    case 1:
                        std::ignore = args.GetReturnValue().Set(
                            data->GetSkillName(nSkillId > 0 ? game::Hand::Left : game::Hand::Right));
                        break;
                    case 2:
                    case 3:
                        args.GetReturnValue().Set(static_cast<uint32_t>(
                            data->GetSkillId((nSkillId - 2) > 0 ? game::Hand::Left : game::Hand::Right)));
                        break;
                    case 4: {
                        auto skills = data->GetAllSkills();
                        auto arr = ub::Array::New(context, static_cast<uint32_t>(skills.size()));
                        if (!arr) {
                            return;
                        }
                        for (uint32_t i = 0; i < skills.size(); ++i) {
                            auto& skill = skills[i];
                            auto skillArr = ub::Array::New(context, 3);
                            if (!skillArr) {
                                return;
                            }
                            if (!skillArr->Set(context, 0, convert::ToJS(isolate, skill.skillId)).value_or(false)) {
                                return;
                            }
                            if (!skillArr->Set(context, 1, convert::ToJS(isolate, skill.baseLevel)).value_or(false)) {
                                return;
                            }
                            if (!skillArr->Set(context, 2, convert::ToJS(isolate, skill.totalLevel)).value_or(false)) {
                                return;
                            }
                            if (!arr->Set(context, i, *skillArr).value_or(false)) {
                                return;
                            }
                        }
                        args.GetReturnValue().Set(*arr);
                        break;
                    }
                    default:
                        args.GetReturnValue().SetFalse();
                        break;
                }
            } else if (args[1].IsNumber()) {
                bool includeExtraLevels = convert::ToBool(context, args[1]);
                std::optional charge = false;
                if (args.Length() >= 3) {
                    if (convert::ToBool(context, args[2])) {
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
        cls, "getParent", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }

            if (data->Type() == UnitType::Object) {
                auto ownerName = data->GetParentName();
                std::ignore = args.GetReturnValue().Set(ownerName);
                return;
            }

            auto owner = data->GetOwner();
            if (!owner) {
                args.GetReturnValue().SetNull();
                return;
            }
            if (auto obj = Wrap(args.GetContext(), std::make_shared<game::Unit>(*owner))) {
                args.GetReturnValue().Set(*obj);
            }
        });

    /// @description Returns this player's hired mercenary as a Unit (Player units only).
    /// @signature getMerc()
    /// @returns {Unit|null} - The mercenary Unit, null if none or not a player, false if the game was not ready.
    Method(
        cls, "getMerc", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            args.GetReturnValue().SetNull();
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
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
            if (auto obj = Wrap(args.GetContext(), std::make_shared<game::Unit>(merc.value()))) {
                args.GetReturnValue().Set(*obj);
            }
        });

    // Reference line 1697: lpUnit ? FindUnit(lpUnit) : GetPlayerUnit(), then GetMercUnit + HP%
    /// @description Returns the owning player's mercenary HP as a percentage (0-100) of its max HP (uses this unit if
    /// valid, else the local player).
    /// @signature getMercHP()
    /// @returns {number} - Mercenary HP percent 0-100 (0 if dead or no max HP), undefined if no merc, false if the game
    /// was not ready.
    Method(
        cls, "getMercHP", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            // Reference: lpUnit ? FindUnit(lpUnit) : GetPlayerUnit()
            auto* data = Unwrap(args.This());
            game::Unit unit;
            if (data != nullptr && *data) {
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
        cls, "getEnchant", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0].IsNumber()) {
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }
            // Reference line 1914: only monsters have enchants
            if (data->Type() != UnitType::Monster) {
                return;
            }
            const auto& context = args.GetContext();
            uint32_t enchantId = convert::ToUint32(context, args[0]);
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
        cls, "getQuest", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 2 || !args[0].IsNumber() || !args[1].IsNumber()) {
                return;
            }
            const auto& context = args.GetContext();
            uint32_t nQuest = convert::ToUint32(context, args[0]);
            uint32_t nFlag = convert::ToUint32(context, args[1]);
            args.GetReturnValue().Set(game::GetQuestFlag(nQuest, nFlag));
        });

    /// @description Tests whether the given status-effect state id is currently active on the unit.
    /// @signature getState(stateId: number)
    /// @param stateId {number} - Required non-negative state id (no upper bound, since mods may add states).
    /// @returns {boolean} - True if the state is active, false otherwise.
    Method(
        cls, "getState", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0].IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            int32_t nState = convert::ToInt32(context, args[0]);
            // No max state check, as mods might add new stats.
            if (nState < 0) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
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
        cls, "getStat", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }

            if (args.Length() < 1 || !args[0].IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }

            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            int32_t statId = convert::ToInt32(context, args[0]);

            uint32_t subIndex = 0;
            if (args.Length() > 1) {
                subIndex = convert::ToInt32(context, args[1]);
            }

            if (statId == -1) {
                // Return flat array of [statId, subIndex, value] sub-arrays
                // Reference: merges pUnit->pStats->StatVec with D2COMMON_GetStatList(pUnit, NULL, 0x40)
                auto allStats = data->GetAllStats();
                auto arr = ub::Array::New(context, static_cast<uint32_t>(allStats.size()));
                if (!arr) {
                    return;
                }
                for (uint32_t i = 0; i < allStats.size(); ++i) {
                    const auto& entry = allStats[i];
                    auto statArr = ub::Array::New(context, 3);
                    if (!statArr || !statArr->Set(context, 0, convert::ToJS(isolate, entry.statId)).value_or(false) ||
                        !statArr->Set(context, 1, convert::ToJS(isolate, entry.subIndex)).value_or(false) ||
                        !statArr->Set(context, 2, convert::ToJS(isolate, entry.value)).value_or(false) ||
                        !arr->Set(context, i, *statArr).value_or(false)) {
                        return;
                    }
                }
                args.GetReturnValue().Set(*arr);
            } else if (statId == -2) {
                // Return sparse array indexed by statId with detailed charge info.
                // Reference: InsertStatsToGenericObject builds sparse array where:
                //   - For wSubIndex > 0x200: creates objects with {skill, level, charges, maxcharges}
                //     (skill = subIndex >> 6, level = subIndex & 0x3F, charges/maxcharges from value)
                //   - For normal stats: creates sub-arrays indexed by subIndex containing the value
                //   - Stats 6-11 (hp/mana/stamina) are right-shifted by 8
                // Delegates to game::Unit::GetDetailedStats() for the raw data.
                auto detailedStats = data->GetDetailedStats();
                auto arr = ub::Array::New(context);
                if (!arr) {
                    return;
                }
                for (const auto& entry : detailedStats) {
                    ub::HandleScope innerScope(isolate);
                    auto existing = arr->Get(context, entry.statId);
                    if (!existing) {
                        return;
                    }
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
                        auto obj = ub::Object::New(context);
                        if (!obj || !obj->Set(context, "skill", convert::ToJS(isolate, skill)).value_or(false) ||
                            !obj->Set(context, "level", convert::ToJS(isolate, level)).value_or(false)) {
                            return;
                        }
                        if (maxcharges > 0 &&
                            (!obj->Set(context, "charges", convert::ToJS(isolate, charges)).value_or(false) ||
                             !obj->Set(context, "maxcharges", convert::ToJS(isolate, maxcharges)).value_or(false))) {
                            return;
                        }
                        // Place at arr[statId]; if already exists, wrap in array
                        if (existing->IsUndefined()) {
                            if (!arr->Set(context, entry.statId, *obj).value_or(false)) {
                                return;
                            }
                        } else if (auto existingArr = existing->To<ub::Array>()) {
                            if (!existingArr->Set(context, existingArr->Length(), *obj).value_or(false)) {
                                return;
                            }
                        } else {
                            auto newArr = ub::Array::New(context, 2);
                            if (!newArr || !newArr->Set(context, 0, *existing).value_or(false) ||
                                !newArr->Set(context, 1, *obj).value_or(false) ||
                                !arr->Set(context, entry.statId, *newArr).value_or(false)) {
                                return;
                            }
                        }
                    } else {
                        // Normal stat: value placed in sub-array at arr[statId][subIndex].
                        // GetDetailedStats already applies the 8.8 fixed-point shift to
                        // stats 6-11 (hp/mana/stamina) - getStat(-2) parity, no extra
                        // shift here. (getStat(-1) keeps raw values; see GetAllStats.)
                        if (existing->IsUndefined()) {
                            auto fresh = ub::Array::New(context);
                            if (!fresh || !arr->Set(context, entry.statId, *fresh).value_or(false)) {
                                return;
                            }
                            existing = *fresh;
                        }
                        if (auto subArr = existing->To<ub::Array>()) {
                            if (!subArr->Set(context, entry.subIndex, convert::ToJS(isolate, entry.value))
                                     .value_or(false)) {
                                return;
                            }
                        }
                    }
                }
                args.GetReturnValue().Set(*arr);
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
                        args.GetReturnValue().Set(static_cast<double>(static_cast<uint32_t>(value)));
                        return;
                    }
                    args.GetReturnValue().Set(value);
                }
            }
        });

    /// @description Returns the unit's stat lists unmerged - one entry per leaf stat array, each tagged with the
    /// game's own `flags` (STATLIST_* bitmask) and `stateNo`, which together say whether the array is the unit's base
    /// stats, an item mod, a set tier or a runeword, and whether it currently contributes. Entries are sorted by
    /// (flags, stateNo) and empty arrays are dropped. A socketed gem or an equipped item keeps its own lists - read
    /// those off that unit. Values carry the same 8.8 fixed-point shift as getStat().
    /// @signature getStatLists()
    /// @returns {Array<{flags:number,stateNo:number,stats:Array<{id:number,layer:number,value:number}>}>} - The stat
    /// lists, empty when the unit has none; false if the game was not ready.
    Method(
        cls, "getStatLists", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }

            auto lock = game::Bridge::Lock();
            auto lists = data->GetStatLists();
            auto arr = ub::Array::New(context, static_cast<uint32_t>(lists.size()));
            if (!arr) {
                return;
            }
            for (uint32_t i = 0; i < lists.size(); ++i) {
                auto list = convert::ToJS(context, lists[i]);
                if (!list || !arr->Set(context, i, *list).value_or(false)) {
                    return;
                }
            }
            args.GetReturnValue().Set(*arr);
        });

    /// @description Returns the item's full flags bitmask (identified, broken, socketed, ethereal, runeword, etc.);
    /// Item units only.
    /// @signature getFlags()
    /// @returns {number} - Item flags bitmask, undefined if not an item, false if the game was not ready.
    Method(
        cls, "getFlags", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
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
        cls, "getFlag", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0].IsNumber()) {
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }
            // Reference line 1163: only items have flags
            if (data->Type() != UnitType::Item) {
                return;
            }
            const auto& context = args.GetContext();
            uint32_t flag = convert::ToUint32(context, args[0]);
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
        cls, "getItemCost", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0].IsNumber()) {
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }
            // Reference line 1243-1244: only items have cost
            if (data->Type() != UnitType::Item) {
                return;
            }
            auto mode = static_cast<game::ItemCostMode>(convert::ToInt32(context, args[0]));
            // Reference line 1277-1287: only modes Buy(0), Sell(1), Repair(2) are valid
            if (mode < game::ItemCostMode::Buy || mode > game::ItemCostMode::Repair) {
                return;
            }
            // Default NPC: use currently interacting NPC, fall back to Charsi
            auto npc = game::Unit::InteractingNPC();
            uint32_t npcClassId = npc ? npc->ClassId() : game::NPC_CHARSI_CLASS_ID;
            auto difficulty = game::GetDifficulty();
            if (args.Length() > 1) {
                if (args[1].IsObject()) {
                    // If NPC passed as a Unit object, unwrap and get its classId
                    auto* npcUnit = Unwrap(args[1]);
                    if (npcUnit && *npcUnit) {
                        npcClassId = npcUnit->ClassId();
                    }
                } else if (args[1].IsNumber()) {
                    npcClassId = convert::ToUint32(context, args[1]);
                }
            }
            if (args.Length() > 2 && args[2].IsNumber()) {
                difficulty = static_cast<game::Difficulty>(convert::ToInt32(context, args[2]));
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
        cls, "setSkill", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
            if (!*data) {
                args.GetReturnValue().SetFalse();
                return;
            }

            uint16_t skillId = 0;
            if (args[0].IsString()) {
                std::string skillName = convert::ToString(context, args[0]);
                auto resolved = game::GetSkillByName(skillName);
                if (!resolved.has_value()) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                skillId = resolved.value();
            } else if (args[0].IsNumber()) {
                skillId = static_cast<uint16_t>(convert::ToInt32(context, args[0]));
            } else {
                args.GetReturnValue().SetFalse();
                return;
            }
            // Reference requires arg[1] to be an int -- returns false if missing or wrong type
            if (args.Length() < 2 || !args[1].IsNumber()) {
                args.GetReturnValue().SetFalse();
                return;
            }
            // JS arg is numeric 0/1 (truthy->leftHand). Preserve that at the binding boundary.
            game::Hand hand = convert::ToBool(context, args[1]) ? game::Hand::Left : game::Hand::Right;
            std::optional<uint32_t> itemId;
            if (args.Length() == 3 && args[2].IsObject()) {
                auto* itemUnit = Unwrap(args[2]);
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
            auto* script = ScriptEngine::Instance().GetScript(&isolate);
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
        cls, "move", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
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
            auto target = (args.Length() >= 2 && args[0].IsNumber() && args[1].IsNumber())
                              ? extract::Position(args, 0).value_or(data->Pos())
                              : data->Pos();
            data->Move(target);
        });

    /// @description Displays an overhead chat/floating-text message above the unit.
    /// @signature overhead(text: string)
    /// @param text {string} - The message to show above the unit (coerced to string; empty string shows nothing).
    /// @returns {boolean} - True once handled; false if the game was not ready.
    Method(
        cls, "overhead", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            args.GetReturnValue().SetFalse();
            if (args.Length() > 0) {
                auto* data = Receiver<JSUnit>(args);
                if (data == nullptr || !*data) {
                    return;
                }
                const auto& context = args.GetContext();
                std::string text = convert::ToString(context, args[0]);
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
        cls, "revive", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
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
        cls, "shop", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1) {
                args.GetReturnValue().SetFalse();
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr) {
                return;
            }
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
            // With no arguments that is `undefined`, as it was when the index was -1.
            const auto modeArg = args.Length() > 0 ? args[args.Length() - 1] : args[0];
            auto mode = static_cast<game::ShopMode>(convert::ToInt32(context, modeArg));
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
        cls, "getMinionCount", +[](const ub::CallbackInfo& args) {
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
                return;
            }
            if (args.Length() < 1 || !args[0].IsNumber()) {
                return;
            }
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }
            // Reference line 1960: only monsters and players have minions
            if (data->Type() != UnitType::Monster && data->Type() != UnitType::Player) {
                return;
            }
            const auto& context = args.GetContext();
            int32_t nType = convert::ToInt32(context, args[0]);
            args.GetReturnValue().Set(data->GetMinionCount(nType));
        });

    /// @description Returns the total gold cost to repair all of the local player's items at an NPC (always uses the
    /// player unit; the unit is ignored).
    /// @signature getRepairCost(npcClassId?: number)
    /// @param npcClassId {number} - Optional NPC class id; defaults to the interacting NPC, else Charsi.
    /// @returns {number} - Total repair cost in gold (0 if no player unit); false if the game was not ready.
    Method(
        cls, "getRepairCost", +[](const ub::CallbackInfo& args) {
            const auto& context = args.GetContext();
            if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                error::WarnAndReturnFalse(args, "Game not ready");
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
            if (args.Length() > 0 && args[0].IsNumber()) {
                npcClassId = convert::ToInt32(context, args[0]);
            }
            args.GetReturnValue().Set(player.GetRepairCost(npcClassId));
        });

    // Symbol.iterator - yields [x, y] so `let [x, y] = unit;` works.
    // Snapshots Pos() at iterator-creation; delegates iteration state to
    // the built-in Array iterator.
    SymbolMethod(
        cls, ub::WellKnownSymbol::Iterator, "[Symbol.iterator]", +[](const ub::CallbackInfo& args) {
            auto& isolate = args.GetIsolate();
            const auto& context = args.GetContext();
            auto* data = Receiver<JSUnit>(args);
            if (data == nullptr || !*data) {
                return;
            }
            auto pos = data->Pos();

            auto arr = ub::Array::New(context, 2);
            if (!arr || !arr->Set(context, 0, convert::ToJS(isolate, pos.x)).value_or(false) ||
                !arr->Set(context, 1, convert::ToJS(isolate, pos.y)).value_or(false)) {
                return;
            }

            auto iteratorKey = ub::Symbol::WellKnown(isolate, ub::WellKnownSymbol::Iterator);
            if (!iteratorKey) {
                return;
            }
            auto iterMethod = arr->Get(context, *iteratorKey);
            if (!iterMethod) {
                return;
            }
            auto iterFn = iterMethod->To<ub::Function>();
            if (!iterFn) {
                return;
            }
            if (auto iter = iterFn->Call(context, *arr)) {
                args.GetReturnValue().Set(*iter);
            }
        });
}

}  // namespace d2bs::api::classes
