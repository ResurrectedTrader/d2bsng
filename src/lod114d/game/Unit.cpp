#include "game/Unit.h"

#include "asm_thunks/asm_thunks.h"
#include "game/Bridge.h"
#include "game/Constants.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/GameThread.h"
#include "game/Room.h"
#include "imports/D2Client.h"
#include "imports/D2Common.h"
#include "imports/D2Lang.h"
#include "imports/D2Net.h"
#include "imports/extras/D2ActiveRoomStrc.h"
#include "imports/extras/D2DrlgLevelStrc.h"
#include "imports/extras/D2DrlgRoomStrc.h"
#include "utils/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2Inventory.h>           // D2InventoryStrc, D2ItemExtraDataStrc
#include <D2Items.h>               // D2C_ItemModes (IMODE_ONGROUND, IMODE_DROPPING)
#include <D2PacketDef.h>           // D2GSPacketClt3C
#include <D2Skills.h>              // D2SkillStrc, D2SkillListStrc
#include <D2StatList.h>            // D2StatStrc, D2StatListExStrc, STAT_*
#include <DataTbls/ItemsTbls.h>    // D2ItemsTxt
#include <DataTbls/ObjectsTbls.h>  // D2ObjectsTxt
#include <DataTbls/SkillsTbls.h>   // D2SkillsTxt
#include <Path/Path.h>             // D2DynamicPathStrc, D2StaticPathStrc
#include <Units/Item.h>            // D2ItemDataStrc
#include <Units/Monster.h>         // D2MonsterDataStrc
#include <Units/Object.h>          // D2ObjectDataStrc
#include <Units/Player.h>          // D2PlayerDataStrc
#include <Units/Units.h>           // D2UnitStrc, D2C_UnitTypes
#pragma clang diagnostic pop

#include <Windows.h>  // Sleep

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace d2bs::game {

namespace {

inline D2UnitStrc* AsUnit(void* p) noexcept {
    return static_cast<D2UnitStrc*>(p);
}

// Reference parity: D2 unit hash tables - 6 type buckets x 128 hash entries.
// Constants and struct now live in imports/extras/D2UnitHashTables.h.
using d2bs::imports::extras::UNIT_HASH_BUCKETS;
using d2bs::imports::extras::UNIT_HASH_TYPE_COUNT;

// Reference parity: D2 stores hp/mana/stamina (stat ids 6..11) in 8.8 fixed point.
constexpr uint32_t STAT_FIXED_POINT_FIRST = 6;
constexpr uint32_t STAT_FIXED_POINT_LAST = 11;
constexpr uint32_t STAT_FIXED_POINT_SHIFT_BITS = 8;

// Reference's `MonsterData::nTypeFlag` packs the four monster-class flags into a
// single byte: bit 1 fNormal, bit 2 fChamp, bit 3 fBoss, bit 4 fMinion. SpecType
// repacks them into the legacy 0x01/0x02/0x04/0x08 layout the JS API expects.
constexpr uint8_t MON_FLAG_NORMAL = 0x02;
constexpr uint8_t MON_FLAG_CHAMP = 0x04;
constexpr uint8_t MON_FLAG_BOSS = 0x08;
constexpr uint8_t MON_FLAG_MINION = 0x10;

constexpr uint32_t SPECTYPE_SUPERUNIQUE = 0x01;
constexpr uint32_t SPECTYPE_CHAMP = 0x02;
constexpr uint32_t SPECTYPE_BOSS = 0x04;
constexpr uint32_t SPECTYPE_MINION = 0x08;

// Reference parity: GetItemPrice's mode argument 0/1 = buy/sell, 3 = repair
// (mode 2 in our enum maps to 3 internally).
constexpr int32_t ITEM_PRICE_MODE_REPAIR = 3;

// Reference: dwOwnerGUID == 0xFFFFFFFF means "no owner" for monster summons.
constexpr uint32_t NO_OWNER_GUID = std::numeric_limits<uint32_t>::max();

// Reference Constants.h:15,28 - UI variable indices passed to GetUIVar.
constexpr uint32_t UI_GAME = 0x00;
constexpr uint32_t UI_NPCSHOP = 0x0C;

// Walk the per-type unit hash table looking for the first non-null bucket.
D2UnitStrc* FirstUnitInTable(const d2bs::imports::extras::D2UnitHashTable* table) {
    if (table == nullptr) {
        return nullptr;
    }
    for (auto* head : table->buckets) {
        if (head != nullptr) {
            return head;
        }
    }
    return nullptr;
}

// Resolve a per-type table pointer for either the server or client unit
// registry. Returns nullptr when the type is out of range.
const d2bs::imports::extras::D2UnitHashTable* TableForType(const d2bs::imports::extras::D2UnitHashTables* tables,
                                                           UnitType type) {
    const auto typeIdx = static_cast<uint32_t>(type);
    if (tables == nullptr || typeIdx >= UNIT_HASH_TYPE_COUNT) {
        return nullptr;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by UNIT_HASH_TYPE_COUNT check above
    return &tables->tables[typeIdx];
}

// Reference parity: monster types live in the server-side table, missiles
// (type 3) live in the client-side table. The factory wraps both lookups.
D2UnitStrc* FindUnitInHashTable(uint32_t id, UnitType type) {
    auto* found = imports::d2client::UNITS_GetServerSideUnit(id, type);
    if (found != nullptr) {
        return found;
    }
    return imports::d2client::UNITS_GetClientSideUnit(id, type);
}

// A skill is treated as "from a charged item" when it's bound to an item
// (nOwnerGUID set) and still has charges remaining. The game also keeps a
// dedicated 1/0 IsCharge byte at offset 0x3C of the Skill struct (used by
// reference d2bs); the byte sits past D2MOO's D2SkillStrc and isn't exposed
// here. We derive the predicate from nOwnerGUID + nCharges instead - the
// game maintains the two views in lockstep, and our derivation is stricter
// (a depleted charge skill returns false), which matches the only intended
// caller: "do I have this charge skill available to cast right now?".
bool IsChargeSkill(const D2SkillStrc* skill) {
    if (skill == nullptr) {
        return false;
    }
    return skill->nCharges > 0 && skill->nOwnerGUID != NO_OWNER_GUID;
}

}  // namespace

void* Unit::ResolvePtr() const {
    GameReadLock guard;
    if (kind_ == UnitKind::Player) {
        if (auto* cached = cache_.Get()) {
            return cached;
        }
        void* resolved = imports::d2client::UNITS_GetPlayerUnit();
        cache_.Set(resolved);
        return resolved;
    }
    // Default-constructed (empty) handle: unitId_=0, type_=Player, kind_=Regular.
    if (unitId_ == 0 && type_ == UnitType::Player) {
        return nullptr;
    }
    if (auto* cached = cache_.Get()) {
        return cached;
    }
    void* resolved = FindUnitInHashTable(unitId_, type_);
    cache_.Set(resolved);
    return resolved;
}

bool Unit::operator==(const Unit& other) const {
    // Pointer identity: handles the sentinel (me object, unitId_=0) because both resolve to GetPlayerUnit().
    auto* lhs = ResolvePtr();
    auto* rhs = other.ResolvePtr();
    return lhs != nullptr && lhs == rhs;
}

Unit Unit::FromPtr(void* p) {
    if (p == nullptr) {
        return {};
    }
    auto* u = AsUnit(p);
    Unit handle;
    handle.unitId_ = u->dwUnitId;
    handle.type_ = static_cast<UnitType>(u->dwUnitType);
    handle.kind_ = UnitKind::Regular;
    // Stash the raw pointer so ResolvePtr returns this exact unit, not a same-(id,type) entry from the other hash
    // table.
    handle.cache_.Set(p);
    return handle;
}

// === Direct struct reads ===

UnitType Unit::Type() const {
    // Return the stored type. Re-resolving an item that is being sold on retry might make it disappear, triggering
    // weird failure cases.
    return type_;
}

uint32_t Unit::ClassId() const {
    auto* u = AsUnit(ResolvePtr());
    return u ? static_cast<uint32_t>(u->dwClassId) : 0U;
}

uint32_t Unit::Mode() const {
    auto* u = AsUnit(ResolvePtr());
    return u ? u->dwAnimMode : 0U;
}

uint32_t Unit::Id() const {
    // When kind_ == Player, unitId_ is always zero, which causes issues when stamping iteration cursor,
    // as it does a simple FindUnit.
    if (kind_ == UnitKind::Player) {
        auto* u = AsUnit(ResolvePtr());
        return u != nullptr ? u->dwUnitId : 0;
    }
    return unitId_;
}

uint32_t Unit::Act() const {
    auto* u = AsUnit(ResolvePtr());
    // Reference: pUnit->dwAct + 1 (game stores 0-based act, JS exposes 1-based).
    return u ? static_cast<uint32_t>(u->nAct) + 1U : 1U;
}

// === Position ===

Position Unit::Pos() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return Position::Zero;
    }
    // GetClientCoordX/Y already apply the *5 subtile scale internally.
    return {.x = imports::d2common::UNITS_GetClientCoordX(u), .y = imports::d2common::UNITS_GetClientCoordY(u)};
}

Position Unit::TargetPos() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return Position::Zero;
    }
    // Only player/monster/missile types carry a dynamic path with target coords.
    switch (u->dwUnitType) {
        case UNIT_PLAYER:
        case UNIT_MONSTER:
        case UNIT_MISSILE: {
            if (u->pDynamicPath == nullptr) {
                return Position::Zero;
            }
            return {.x = u->pDynamicPath->tTargetCoord.X, .y = u->pDynamicPath->tTargetCoord.Y};
        }
        default:
            return Position::Zero;
    }
}

uint32_t Unit::Area() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    auto* room = imports::d2common::UNITS_GetRoom(u);
    if (room == nullptr || room->pDrlgRoom == nullptr || room->pDrlgRoom->pLevel == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(room->pDrlgRoom->pLevel->nLevelId);
}

// === Stats ===

uint32_t Unit::Hp() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    // For hirelings, STAT_HP is encoded as a percentage instead of the actual
    // HP value, so STAT_HP and STAT_MAXHP are in mismatched scales. Reference
    // uses D2CLIENT_GetUnitHPPercent (JSUnit.cpp:1702). A monster is a
    // hireling when its owner is a player unit. The percent field at the
    // RosterPets table offset +0x1C is initialised to 100 (sub_478B10) so the
    // range is 0-100, not 0-128.
    if (u->dwUnitType == UNIT_MONSTER) {
        const uint32_t ownerId = imports::d2client::MONSTERS_GetOwner(u->dwUnitId);
        if (ownerId != NO_OWNER_GUID) {
            auto* ownerUnit = imports::d2client::UNITS_GetClientSideUnit(ownerId, UnitType::Player);
            if (ownerUnit == nullptr) {
                ownerUnit = imports::d2client::UNITS_GetServerSideUnit(ownerId, UnitType::Player);
            }
            if (ownerUnit != nullptr) {
                const auto percent = imports::d2common::UNITS_GetCurrentLifePercentage(u->dwUnitId);
                const auto maxHp =
                    static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_MAXHP, 0)) >>
                    STAT_FIXED_POINT_SHIFT_BITS;
                return (percent * maxHp) / 100U;
            }
        }
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_HITPOINTS, 0)) >>
           STAT_FIXED_POINT_SHIFT_BITS;
}

uint32_t Unit::HpMax() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_MAXHP, 0)) >>
           STAT_FIXED_POINT_SHIFT_BITS;
}

uint32_t Unit::Mp() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_MANA, 0)) >>
           STAT_FIXED_POINT_SHIFT_BITS;
}

uint32_t Unit::MpMax() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_MAXMANA, 0)) >>
           STAT_FIXED_POINT_SHIFT_BITS;
}

uint32_t Unit::Stamina() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_STAMINA, 0)) >>
           STAT_FIXED_POINT_SHIFT_BITS;
}

uint32_t Unit::StaminaMax() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_MAXSTAMINA, 0)) >>
           STAT_FIXED_POINT_SHIFT_BITS;
}

uint32_t Unit::CharLevel() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(imports::d2common::STATLIST_UnitGetStatValue(u, STAT_LEVEL, 0));
}

int32_t Unit::GetStat(uint32_t stat, uint32_t sub) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return 0;
    }

    int32_t value = imports::d2common::STATLIST_UnitGetStatValue(u, stat, sub);

    // Preset stat fallback: if the regular getter returned 0, search the
    // item-level (preset) stat list. Reference parity: JSUnit.cpp:980-993
    // copies the full preset stat list and linear-scans it for the requested
    // (stat, sub) pair - no per-stat preset getter exists.
    if (value == 0) {
        if (auto* preset = imports::d2common::STATLIST_GetStatListFromUnitStateAndFlag(u, 0U, STAT_LIST_PRESET_FLAG)) {
            std::array<D2StatStrc, 256> buf{};
            const auto count = imports::d2common::STATLIST_CopyStats(preset, buf.data(), buf.size());
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by count<=256
            for (uint32_t i = 0; i < count; ++i) {
                const auto& s = buf[i];
                if (s.nStat == stat && s.nLayer == sub) {
                    value = s.nValue;
                    break;
                }
            }
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
        }
    }

    if (stat >= STAT_FIXED_POINT_FIRST && stat <= STAT_FIXED_POINT_LAST) {
        return value >> STAT_FIXED_POINT_SHIFT_BITS;
    }
    return value;
}

bool Unit::HasState(uint32_t stateId) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return false;
    }
    return imports::d2common::STATES_CheckState(u, stateId) != 0;
}

namespace {

// Append every (stat,sub,value) triple from a D2StatStrc array into `out`
// without modifying values. Used by getStat(-1) (`GetAllStats`) which exposes
// raw 8.8 fixed-point values for hp/mana/stamina stats - reference parity:
// JSUnit.cpp:933-947 copies StatVec entries verbatim with no shift.
void AppendStatsRaw(const D2StatStrc* stats, uint32_t count, std::vector<StatEntry>& out) {
    for (uint32_t i = 0; i < count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - bounded by count
        const auto& s = stats[i];
        out.push_back(StatEntry{
            .statId = s.nStat,
            .subIndex = s.nLayer,
            .value = s.nValue,
        });
    }
}

// Append every (stat,sub,value) triple from a D2StatStrc array into `out`,
// applying the 8.8 fixed-point shift for hp/mana/stamina stats. Used by
// getStat(-2) (`GetDetailedStats`) which mirrors reference's
// `InsertStatsNow` (JSUnit.cpp:1071-1075).
void AppendStatsShifted(const D2StatStrc* stats, uint32_t count, std::vector<StatEntry>& out) {
    for (uint32_t i = 0; i < count; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - bounded by count
        const auto& s = stats[i];
        int32_t value = s.nValue;
        if (s.nStat >= STAT_FIXED_POINT_FIRST && s.nStat <= STAT_FIXED_POINT_LAST) {
            value >>= STAT_FIXED_POINT_SHIFT_BITS;
        }
        out.push_back(StatEntry{
            .statId = s.nStat,
            .subIndex = s.nLayer,
            .value = value,
        });
    }
}

bool StatEntryEqual(const StatEntry& a, const StatEntry& b) {
    return a.statId == b.statId && a.subIndex == b.subIndex && a.value == b.value;
}

}  // namespace

std::vector<StatEntry> Unit::GetAllStats() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pStatListEx == nullptr) {
        return {};
    }
    auto* baseList = static_cast<D2StatListStrc*>(u->pStatListEx);
    std::vector<StatEntry> out;

    // Reference (getStat(-1), JSUnit.cpp:924-948) copies StatVec entries
    // verbatim - raw 8.8 fixed-point values for hp/mana/stamina, no shift.
    // The 8.8->int shift only happens on the getStat(-2) path.

    // Start with the preset stat list (D2COMMON_GetStatList(unit, nullptr, 0x40)
    // followed by D2COMMON_CopyStatList).
    if (auto* preset = imports::d2common::STATLIST_GetStatListFromUnitStateAndFlag(u, 0U, STAT_LIST_PRESET_FLAG)) {
        std::array<D2StatStrc, 256> buf{};
        const auto count = imports::d2common::STATLIST_CopyStats(preset, buf.data(), buf.size());
        AppendStatsRaw(buf.data(), count, out);
    }

    // Merge in pStatListEx->Stats - dedup against entries already present from
    // the preset list (reference: JSUnit.cpp:933-947).
    const auto& vec = baseList->Stats;
    if (vec.pStat != nullptr) {
        for (uint32_t i = 0; i < vec.nStatCount; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - bounded by nStatCount
            const auto& s = vec.pStat[i];
            const StatEntry candidate{
                .statId = s.nStat,
                .subIndex = s.nLayer,
                .value = s.nValue,
            };
            const bool already =
                std::ranges::any_of(out, [&candidate](const StatEntry& e) { return StatEntryEqual(e, candidate); });
            if (!already) {
                out.push_back(candidate);
            }
        }
    }
    return out;
}

std::vector<StatEntry> Unit::GetDetailedStats() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pStatListEx == nullptr) {
        return {};
    }
    std::vector<StatEntry> out;

    auto appendList = [&out](D2StatListStrc* list) {
        if (list == nullptr) {
            return;
        }
        const auto& vec = list->Stats;
        if (vec.pStat != nullptr) {
            AppendStatsShifted(vec.pStat, vec.nStatCount, out);
        }
        // Reference: `pStatList->dwFlags >> 24 & 0x80` is `dwFlags & 0x80000000`,
        // which is STATLIST_EXTENDED. STATLIST_StatListExCast already gates on
        // exactly that bit, so just rely on it - D2MOO's FullStats is the same
        // bucket reference walks via SetStatVec.
        if (auto* ex = STATLIST_StatListExCast(list)) {
            if (ex->FullStats.pStat != nullptr) {
                AppendStatsShifted(ex->FullStats.pStat, ex->FullStats.nStatCount, out);
            }
        }
    };

    appendList(static_cast<D2StatListStrc*>(u->pStatListEx));
    appendList(imports::d2common::STATLIST_GetStatListFromUnitStateAndFlag(u, 0U, STAT_LIST_PRESET_FLAG));
    return out;
}

// === Name ===

// Reference parity: GetUnitName(pUnit) - picks the right name source per type.
std::string Unit::Name() const {
    auto* unit = AsUnit(ResolvePtr());
    if (unit == nullptr) {
        return {};
    }
    switch (unit->dwUnitType) {
        case UNIT_PLAYER: {
            if (unit->pPlayerData == nullptr) {
                return {};
            }
            return std::string{static_cast<const char*>(unit->pPlayerData->szName)};
        }
        case UNIT_MONSTER: {
            const auto* wName = imports::d2client::UNITS_GetName(unit);
            return wName ? d2bs::utils::ToStr(std::wstring{wName}) : std::string{};
        }
        case UNIT_ITEM: {
            std::array<wchar_t, 256> buf{};
            imports::d2client::ITEMS_GetName(unit, buf.data(), buf.size());
            std::wstring s(buf.data());
            // Reference strips a trailing locale-name newline ("\nname\nattrs").
            if (auto nl = s.find(L'\n'); nl != std::wstring::npos) {
                s.resize(nl);
            }
            return d2bs::utils::ToStr(s);
        }
        case UNIT_OBJECT:
        case UNIT_TILE: {
            if (unit->pObjectData == nullptr || unit->pObjectData->pObjectTxt == nullptr) {
                return {};
            }
            return std::string{static_cast<const char*>(unit->pObjectData->pObjectTxt->szName)};
        }
        default:
            return {};
    }
}

std::string Unit::ItemFullName() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return {};
    }
    // 256 wchars matches reference (JSUnit.cpp:1538, D2Helpers.cpp).
    std::array<wchar_t, 256> buf{};
    imports::d2client::ITEMS_GetName(u, buf.data(), buf.size());
    return d2bs::utils::ToStr(std::wstring{buf.data()});
}

// === Unit info ===

uint32_t Unit::Direction() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pDynamicPath == nullptr || u->pDynamicPath->pRoom == nullptr) {
        return 0U;
    }
    return u->pDynamicPath->nDirection;
}

std::optional<uint32_t> Unit::UniqueId() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return std::nullopt;
    }
    if (u->dwUnitType == UNIT_MONSTER) {
        if (u->pMonsterData == nullptr) {
            return std::nullopt;
        }
        // Reference: monsters expose the super-unique row index only when both the
        // boss and normal flags are set. Anything else returns -1 to JS, expressed
        // here as `nullopt` (the binding coerces it).
        const auto flags = u->pMonsterData->nTypeFlag;
        if ((flags & MON_FLAG_BOSS) == 0 || (flags & MON_FLAG_NORMAL) == 0) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(u->pMonsterData->wBossHcIdx);
    }
    if (u->dwUnitType == UNIT_ITEM) {
        if (u->pItemData == nullptr ||
            (u->pItemData->dwQualityNo != ITEMQUAL_UNIQUE && u->pItemData->dwQualityNo != ITEMQUAL_SET)) {
            return std::nullopt;
        }
        return u->pItemData->dwFileIndex;
    }
    return std::nullopt;
}

uint32_t Unit::SpecType() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_MONSTER || u->pMonsterData == nullptr) {
        return 0U;
    }
    const auto flags = u->pMonsterData->nTypeFlag;
    uint32_t spec = 0U;
    if ((flags & MON_FLAG_MINION) != 0) {
        spec |= SPECTYPE_MINION;
    }
    if ((flags & MON_FLAG_BOSS) != 0) {
        spec |= SPECTYPE_BOSS;
    }
    if ((flags & MON_FLAG_CHAMP) != 0) {
        spec |= SPECTYPE_CHAMP;
    }
    if ((flags & MON_FLAG_BOSS) != 0 && (flags & MON_FLAG_NORMAL) != 0) {
        spec |= SPECTYPE_SUPERUNIQUE;
    }
    return spec;
}

uint32_t Unit::ItemCount() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pInventory == nullptr) {
        return 0U;
    }
    return u->pInventory->dwItemCount;
}

std::optional<Unit> Unit::GetOwner() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return std::nullopt;
    }
    switch (u->dwUnitType) {
        case UNIT_MONSTER: {
            const auto ownerId = imports::d2client::MONSTERS_GetOwner(u->dwUnitId);
            if (ownerId == NO_OWNER_GUID) {
                return std::nullopt;
            }
            return Unit::Find(ownerId, std::nullopt);
        }
        case UNIT_MISSILE: {
            auto* owner = imports::d2common::MISSILE_GetOwnerUnit(u);
            if (owner == nullptr) {
                return std::nullopt;
            }
            return Unit::FromPtr(owner);
        }
        case UNIT_ITEM: {
            if (u->pItemData == nullptr || u->pItemData->pExtraData.pParentInv == nullptr ||
                u->pItemData->pExtraData.pParentInv->pOwner == nullptr) {
                return std::nullopt;
            }
            return Unit::FromPtr(u->pItemData->pExtraData.pParentInv->pOwner);
        }
        default:
            return std::nullopt;
    }
}

// === Item-specific ===

std::string Unit::ItemCode() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM) {
        return {};
    }
    auto* txt = imports::d2common::DATATBLS_GetItemsTxtRecord(u->dwClassId);
    if (txt == nullptr) {
        return {};
    }
    // szCode is a 4-char field (last byte reserved). Reference: read 3 bytes
    // verbatim - the field is not guaranteed to be null-terminated within the
    // 4-byte slot.
    return {&txt->szCode[0], 3};
}

std::string Unit::Prefix() const {
    const auto code = PrefixNum();
    if (code == 0) {
        return {};
    }
    const auto* str = imports::d2common::ITEMS_GetMagicalMods(code);
    return str ? std::string(str) : std::string{};
}

std::string Unit::Suffix() const {
    const auto code = SuffixNum();
    if (code == 0) {
        return {};
    }
    const auto* str = imports::d2common::ITEMS_GetMagicalMods(code);
    return str ? std::string(str) : std::string{};
}

uint16_t Unit::PrefixNum() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->wMagicPrefix[0];
}

uint16_t Unit::SuffixNum() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->wMagicSuffix[0];
}

uint16_t Unit::AutoAffixNum() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->wAutoAffix;
}

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> Unit::Prefixes() const {
    static_assert(MAX_AFFIX_SLOTS == ITEMS_MAX_MODS,
                  "Unit::MAX_AFFIX_SLOTS must mirror D2MOO ITEMS_MAX_MODS for binary parity");
    std::array<std::optional<std::string>, MAX_AFFIX_SLOTS> out;
    const auto codes = PrefixNums();
    for (size_t i = 0; i < MAX_AFFIX_SLOTS; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        if (codes[i] == 0) {
            continue;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        if (const auto* str = imports::d2common::ITEMS_GetMagicalMods(codes[i])) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
            out[i] = std::string(str);
        }
    }
    return out;
}

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> Unit::Suffixes() const {
    std::array<std::optional<std::string>, MAX_AFFIX_SLOTS> out;
    const auto codes = SuffixNums();
    for (size_t i = 0; i < MAX_AFFIX_SLOTS; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        if (codes[i] == 0) {
            continue;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        if (const auto* str = imports::d2common::ITEMS_GetMagicalMods(codes[i])) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
            out[i] = std::string(str);
        }
    }
    return out;
}

std::array<uint16_t, Unit::MAX_AFFIX_SLOTS> Unit::PrefixNums() const {
    std::array<uint16_t, MAX_AFFIX_SLOTS> out{};
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return out;
    }
    for (uint32_t i = 0; i < MAX_AFFIX_SLOTS; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        out[i] = u->pItemData->wMagicPrefix[i];
    }
    return out;
}

std::array<uint16_t, Unit::MAX_AFFIX_SLOTS> Unit::SuffixNums() const {
    std::array<uint16_t, MAX_AFFIX_SLOTS> out{};
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return out;
    }
    for (uint32_t i = 0; i < MAX_AFFIX_SLOTS; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        out[i] = u->pItemData->wMagicSuffix[i];
    }
    return out;
}

ItemQuality Unit::Quality() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return ItemQuality::Inferior;
    }
    return static_cast<ItemQuality>(u->pItemData->dwQualityNo);
}

NodePage Unit::Node() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return NodePage::Equipped;
    }
    return static_cast<NodePage>(u->pItemData->pExtraData.nNodePosOther);
}

ItemLocation Unit::ItemLocation() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return d2bs::game::ItemLocation::Null;
    }
    return static_cast<d2bs::game::ItemLocation>(u->pItemData->pExtraData.nNodePos);
}

Size Unit::Size() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return Size::Zero;
    }
    auto* txt = imports::d2common::DATATBLS_GetItemsTxtRecord(u->dwClassId);
    if (txt == nullptr) {
        return Size::Zero;
    }
    return {.width = txt->nInvWidth, .height = txt->nInvHeight};
}

uint32_t Unit::ItemType() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    auto* txt = imports::d2common::DATATBLS_GetItemsTxtRecord(u->dwClassId);
    if (txt == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(txt->wType[0]);
}

std::string Unit::Description() const {
    const auto unitId = Id();
    const auto unitType = Type();
    if (unitId == 0 || unitType != UnitType::Item) {
        return {};
    }
    // Reference (JSUnit.cpp:444-466) uses WriteProcessMemory to set the
    // ItemDescFlag + ItemDescItem globals, calls LoadItemDesc, then reads
    // the rendered description out of a static buffer in D2Win.
    //
    // We're in-process so the WPM dance is unnecessary - direct writes to
    // the same globals + a direct read from the same buffer is equivalent.
    // Resolved buffer location: sub_502280 (the screen-text store function)
    // wcscpy's its first arg into `unk_841EC8` (RVA 0x441EC8 in 1.14d), and
    // LoadItemDesc reaches sub_502280 with the description wstring on the
    // ItemDescFlag != 0 branch.
    //
    // Re-resolve the raw pointer on the game thread (matches ClickMapAt /
    // SubmitItem pattern in GameHelpers.cpp) - capturing the raw D2UnitStrc*
    // across the thread hop would risk a use-after-free if the item is
    // freed between resolve and execute.
    return GameThread::Execute([unitId, unitType]() -> std::string {
        auto* u = imports::d2client::UNITS_GetServerSideUnit(unitId, unitType);
        if (u == nullptr) {
            u = imports::d2client::UNITS_GetClientSideUnit(unitId, unitType);
        }
        if (u == nullptr || u->pItemData == nullptr) {
            return {};
        }
        if (u->pItemData->pExtraData.pParentInv == nullptr || u->pItemData->pExtraData.pParentInv->pOwner == nullptr) {
            return {};
        }
        auto* savedItem = *imports::d2client::gpItemDescItem;
        *imports::d2client::gbItemDescFlag = 1;
        *imports::d2client::gpItemDescItem = u;
        imports::d2client::ITEMS_LoadDescription(u->pItemData->pExtraData.pParentInv->pOwner, 0);
        *imports::d2client::gbItemDescFlag = 0;

        auto& buf = *imports::d2client::gwszItemDescBuffer;
        const auto len = wcsnlen(buf.data(), buf.size());
        auto result = utils::ToStr(std::wstring(buf.data(), len));
        *imports::d2client::gpItemDescItem = savedItem;
        return result;
    });
}

BodyLocation Unit::BodyLocation() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return d2bs::game::BodyLocation::None;
    }
    return static_cast<d2bs::game::BodyLocation>(u->pItemData->nBodyLoc);
}

uint32_t Unit::ItemLevel() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->dwItemLevel;
}

uint32_t Unit::LevelRequirement() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM) {
        return 0U;
    }
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    return imports::d2common::ITEMS_GetLevelRequirement(u, player);
}

uint32_t Unit::GfxIndex() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->nInvGfxIdx;
}

uint32_t Unit::ItemFlags() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return 0U;
    }
    return u->pItemData->dwItemFlags;
}

uint32_t Unit::ItemCost(ItemCostMode mode, uint32_t npcClassId, Difficulty difficulty) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM) {
        return 0U;
    }

    // Reference parity: validate npcClassId has a monstats inventory; fall back
    // to Charsi when the lookup fails. The validation lives here (not at the JS
    // boundary) so every call site through ItemCost gets it for free.
    int64_t inventoryRow = 0;
    auto invVal = GetTxtValue("monstats", npcClassId, "inventory");
    if (auto* num = std::get_if<int64_t>(&invVal)) {
        inventoryRow = *num;
    }
    const uint32_t resolvedNpc = inventoryRow == 0 ? NPC_CHARSI_CLASS_ID : npcClassId;

    // Mode 2 (Repair) maps to internal mode 3 per reference JSUnit.cpp:1283.
    int32_t internalMode = static_cast<int32_t>(mode);
    if (mode == ItemCostMode::Repair) {
        internalMode = ITEM_PRICE_MODE_REPAIR;
    }

    return imports::d2common::ITEMS_GetTransactionCost(
        imports::d2client::UNITS_GetPlayerUnit(), u, static_cast<D2C_Difficulties>(difficulty),
        *imports::d2client::gpItemPriceList, resolvedNpc, static_cast<D2C_TransactionTypes>(internalMode));
}

// === Object-specific ===

uint32_t Unit::ObjType() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_OBJECT || u->pObjectData == nullptr) {
        return 0U;
    }
    // Reference: when the object lives in a level (GetLevelNoFromRoom != 0)
    // mask off the chest-locked bit; otherwise expose the raw byte.
    auto* room = imports::d2common::UNITS_GetRoom(u);
    if (room != nullptr && imports::d2common::DUNGEON_GetLevelIdFromRoom(room) != 0) {
        constexpr uint32_t TYPE_MASK = 0xFF;
        return u->pObjectData->InteractType & TYPE_MASK;
    }
    return u->pObjectData->InteractType;
}

bool Unit::IsLocked() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_OBJECT || u->pObjectData == nullptr) {
        return false;
    }
    // Object `InteractType` packs the chest-locked bit at 0x80; the remaining 7 bits
    // are the chest type id.
    constexpr uint8_t CHEST_LOCKED_BIT = 0x80;
    return (u->pObjectData->InteractType & CHEST_LOCKED_BIT) != 0;
}

// === Player-specific ===

uint32_t Unit::RunWalk() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u != imports::d2client::UNITS_GetPlayerUnit()) {
        return 0U;
    }
    return *imports::d2client::gbAlwaysRun;
}

uint32_t Unit::WeaponSwitch() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u != imports::d2client::UNITS_GetPlayerUnit()) {
        return 0U;
    }
    return *imports::d2client::gnWeaponSwitch;
}

// === Traversal ===

std::optional<Unit> Unit::GetFirstItem() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pInventory == nullptr) {
        return std::nullopt;
    }
    auto* item = imports::d2common::INVENTORY_GetFirstItem(u->pInventory);
    if (item == nullptr) {
        return std::nullopt;
    }
    // Items obtained via the inventory walk carry their owner's identity in
    // the cursor and route subsequent .getNext() through FindNextInventoryItem
    // (owner-guarded). Without this kind tag, getNext() would route through
    // FindNext (the hash-table walker), which walks the global item table and
    // would return items from other inventories (e.g. vendor stock).
    Unit handle = Unit::FromPtr(item);
    handle.kind_ = UnitKind::InventoryItem;
    handle.cursor_.ownerId = u->dwUnitId;
    handle.cursor_.ownerType = static_cast<UnitType>(u->dwUnitType);
    return handle;
}

std::optional<Unit> Unit::GetNextItem() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return std::nullopt;
    }
    // Reference parity (Unit.cpp:GetInvNextUnit): the game's pNextItem chain
    // can cross from the player's inventory into another unit's (e.g. the
    // vendor's, while a shop UI is open). Stop iteration if we'd cross by
    // checking that the next item's parent inventory matches the current
    // item's.
    auto* curInv = u->pItemData->pExtraData.pParentInv;
    if (curInv == nullptr) {
        return std::nullopt;
    }
    auto* next = imports::d2common::INVENTORY_GetNextItem(u);
    if (next == nullptr || next->pItemData == nullptr) {
        return std::nullopt;
    }
    if (next->pItemData->pExtraData.pParentInv != curInv) {
        return std::nullopt;
    }
    Unit handle = Unit::FromPtr(next);
    handle.kind_ = UnitKind::InventoryItem;
    handle.cursor_ = cursor_;  // propagate owner anchor from the previous item
    return handle;
}

std::optional<Unit> Unit::GetFirstInGame(UnitType type) {
    GameReadLock guard;
    const auto* tables = (type == UnitType::Missile) ? imports::d2client::gClientSideUnitHashTables.Ptr()
                                                     : imports::d2client::gServerSideUnitHashTables.Ptr();
    const auto* table = TableForType(tables, type);
    if (auto* head = FirstUnitInTable(table)) {
        return Unit::FromPtr(head);
    }
    return std::nullopt;
}

std::optional<Unit> Unit::GetNextInGame() const {
    GameReadLock guard;
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return std::nullopt;
    }
    if (auto* next = u->pListNext) {
        return Unit::FromPtr(next);
    }
    const auto* tables = (u->dwUnitType == UNIT_MISSILE) ? imports::d2client::gClientSideUnitHashTables.Ptr()
                                                         : imports::d2client::gServerSideUnitHashTables.Ptr();
    const auto* table = TableForType(tables, static_cast<UnitType>(u->dwUnitType));
    if (table == nullptr) {
        return std::nullopt;
    }
    const uint32_t bucket = u->dwUnitId & 0x7FU;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by UNIT_HASH_BUCKETS
    for (uint32_t b = bucket + 1; b < UNIT_HASH_BUCKETS; ++b) {
        if (auto* head = table->buckets[b]) {
            return Unit::FromPtr(head);
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return std::nullopt;
}

std::string Unit::GetParentName() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_OBJECT || u->pObjectData == nullptr) {
        return {};
    }
    return std::string{static_cast<const char*>(u->pObjectData->szOwner)};
}

Room Unit::GetRoom() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return Room{};
    }
    auto* room = imports::d2common::UNITS_GetRoom(u);
    if (room == nullptr || room->pDrlgRoom == nullptr) {
        return Room{};
    }
    return Room::FromPtr(room->pDrlgRoom);
}

// === Skill methods ===

namespace {

// Pick the equipped skill for the requested hand from the unit's skill list.
const D2SkillStrc* PickEquippedSkill(D2UnitStrc* u, Hand hand) {
    if (u == nullptr || u->pSkills == nullptr) {
        return nullptr;
    }
    return hand == Hand::Right ? u->pSkills->pRightSkill : u->pSkills->pLeftSkill;
}

}  // namespace

std::string Unit::GetSkillName(Hand hand) const {
    auto* u = AsUnit(ResolvePtr());
    const auto* skill = PickEquippedSkill(u, hand);
    if (skill == nullptr || skill->pSkillsTxt == nullptr) {
        return {};
    }
    const auto skillId = static_cast<uint16_t>(skill->pSkillsTxt->nSkillId);
    // Resolve display name through the standard skills->skilldesc->str-name->GetLocaleText chain.
    auto skillDescVal = GetTxtValue("skills", skillId, "skilldesc");
    auto* descRow = std::get_if<int64_t>(&skillDescVal);
    if (descRow == nullptr) {
        return {};
    }
    auto strNameVal = GetTxtValue("skilldesc", static_cast<uint32_t>(*descRow), "str name");
    auto* strRow = std::get_if<int64_t>(&strNameVal);
    if (strRow == nullptr) {
        return {};
    }
    const auto* localized = imports::d2lang::D2LANG_GetLocaleText(static_cast<uint16_t>(*strRow));
    return localized ? d2bs::utils::ToStr(std::wstring{localized}) : std::string{};
}

uint16_t Unit::GetSkillId(Hand hand) const {
    auto* u = AsUnit(ResolvePtr());
    const auto* skill = PickEquippedSkill(u, hand);
    if (skill == nullptr || skill->pSkillsTxt == nullptr) {
        return 0U;
    }
    return static_cast<uint16_t>(skill->pSkillsTxt->nSkillId);
}

std::vector<Unit::SkillInfo> Unit::GetAllSkills() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pSkills == nullptr) {
        return {};
    }
    std::vector<SkillInfo> out;
    for (auto* skill = u->pSkills->pFirstSkill; skill != nullptr; skill = skill->pNextSkill) {
        if (skill->pSkillsTxt == nullptr) {
            continue;
        }
        // D2MOO's `nSkillLevel` is reference's `dwSkillLevel` (offset 0x28).
        out.push_back(SkillInfo{
            .skillId = static_cast<uint16_t>(skill->pSkillsTxt->nSkillId),
            .baseLevel = static_cast<uint32_t>(skill->nSkillLevel),
            .totalLevel = imports::d2common::SKILLS_GetSkillLevel(u, skill, true),
        });
    }
    return out;
}

std::optional<uint32_t> Unit::GetSkillLevel(uint16_t skillId, bool includeExtraLevels,
                                            std::optional<bool> charge) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->pSkills == nullptr) {
        return std::nullopt;
    }
    // charge filter:
    //   false   -> non-charge skills only.
    //   nullopt -> no filter (any skill).
    //   true    -> charge skills only.
    for (auto* skill = u->pSkills->pFirstSkill; skill != nullptr; skill = skill->pNextSkill) {
        if (skill->pSkillsTxt == nullptr || static_cast<uint16_t>(skill->pSkillsTxt->nSkillId) != skillId) {
            continue;
        }
        if (charge.has_value() && IsChargeSkill(skill) != charge.value()) {
            continue;
        }
        return imports::d2common::SKILLS_GetSkillLevel(u, skill, includeExtraLevels);
    }
    return std::nullopt;
}

// === Actions ===

void Unit::Move(Position target) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return;
    }
    // Player movement uses the script-supplied destination; for non-player
    // units the click resolves against the unit's own position.
    const Point click = (u == imports::d2client::UNITS_GetPlayerUnit()) ? target.ToPoint() : Pos().ToPoint();
    // ClickMapAt handles MapToAbsScreen translation, viewport offset, mouse
    // save/restore, AlwaysRun flag, and game-thread dispatch. The press/release
    // pair (clickType 0 then 2) with a small delay matches reference's pattern
    // so the game sees a complete click event.
    ClickMapAt(0U, false, click);
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(50ms);
    ClickMapAt(2U, false, click);
}

bool Unit::Interact() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u == imports::d2client::UNITS_GetPlayerUnit()) {
        return false;
    }

    // Items in inventory/stash use packet 0x20; belt items use 0x26. Reference
    // hard-codes these because the underlying ClickItem path is gated behind
    // additional UI state that the script context cannot drive directly.
    //
    // Reference parity (JSUnit.cpp:832): gate the inventory-style branch on
    // dwAnimMode being neither IMODE_ONGROUND (3) nor IMODE_DROPPING (5).
    // nNodePos on a ground-mode item still carries "the last container it was
    // in", which can equal Inventory(1) or Stash(5), so the dwMode check is
    // the canonical filter - without it, picking up a dropped stash item would
    // mis-fire packet 0x20 with stale node coords.
    if (u->dwUnitType == UNIT_ITEM && u->pItemData != nullptr && u->dwItemMode != IMODE_ONGROUND &&
        u->dwItemMode != IMODE_DROPPING) {
        const auto location = static_cast<d2bs::game::ItemLocation>(u->pItemData->pExtraData.nNodePos);
        if (location == d2bs::game::ItemLocation::Inventory || location == d2bs::game::ItemLocation::Stash) {
            auto* player = imports::d2client::UNITS_GetPlayerUnit();
            D2GSPacketClt20 packet{};
            packet.nHeader = 0x20U;
            packet.nItemGUID = static_cast<int32_t>(u->dwUnitId);
            packet.nPosX = static_cast<int32_t>(player ? imports::d2common::UNITS_GetClientCoordX(player) : 0);
            packet.nPosY = static_cast<int32_t>(player ? imports::d2common::UNITS_GetClientCoordY(player) : 0);
            imports::d2net::CLIENT_Send(sizeof(packet), 1U, reinterpret_cast<uint8_t*>(&packet));
            return false;
        }
        if (location == d2bs::game::ItemLocation::Belt) {
            D2GSPacketClt26 packet{};
            packet.nHeader = 0x26U;
            packet.nItemGUID = static_cast<int32_t>(u->dwUnitId);
            imports::d2net::CLIENT_Send(sizeof(packet), 1U, reinterpret_cast<uint8_t*>(&packet));
            return false;
        }
    }

    // Default path: a left-click on the unit's screen position via the
    // ClickMapAt(Unit&) overload, which handles screen translation, viewport
    // offsets, mouse save/restore, selected-unit bookkeeping, and game-thread
    // dispatch.
    return ClickMapAt(0U, false, *this);
}

bool Unit::TakeWaypoint(uint32_t waypointId) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return false;
    }
    // Reference JSUnit.cpp:864-866: validate against levels.txt - `Waypoint`
    // column carries 255 for any level row that no waypoint reaches. Sending
    // the asm thunk for such an id can crash the game (see the historical
    // "check the range on argv[0]" TODO at reference:855).
    constexpr int64_t WAYPOINT_INVALID_SENTINEL = 255;
    auto wpVal = GetTxtValue("levels", waypointId, "Waypoint");
    if (auto* num = std::get_if<int64_t>(&wpVal)) {
        if (*num == WAYPOINT_INVALID_SENTINEL) {
            return false;
        }
    }

    // Reference JSUnit.cpp:868 calls `D2CLIENT_TakeWaypoint(pUnit->dwUnitId,
    // nWaypointID)` where the first arg is the waypoint object's id and the
    // second is the destination area. The framework's `waypointId` param is
    // the destination-area value (matches reference's nWaypointID).
    asm_thunks::TakeWaypoint(u->dwUnitId, waypointId);

    // Reference JSUnit.cpp:869-870: when the in-game UI didn't pop back open
    // (e.g. because the destination is the same act and the menu state stayed
    // in the waypoint dialog), explicitly close the interact UI so subsequent
    // scripts don't see a stale interaction state.
    if (imports::d2client::UI_GetVar(UI_GAME) == 0) {
        imports::d2client::UI_CloseInteract();
    }
    return true;
}

void Unit::Repair() const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return;
    }
    // Reference: 17-byte repair packet with the recent-interact id at offset 1
    // and a constant 0x80 trailer byte.
    std::array<uint8_t, 17> packet{};
    packet[0] = 0x35U;
    const uint32_t interactId = *imports::d2client::gnRecentInteractId;
    std::memcpy(packet.data() + 1, &interactId, sizeof(uint32_t));
    packet[16] = 0x80U;
    imports::d2net::CLIENT_Send(packet.size(), 1U, packet.data());
}

ClickResult Unit::EquipItem() const {
    if (*imports::d2client::gpTransactionDialog != nullptr || *imports::d2client::gnTransactionDialogs != 0 ||
        *imports::d2client::gnTransactionDialogs_2 != 0) {
        return ClickResult::TransactionInProgress;
    }
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return ClickResult::InvalidTarget;
    }
    const uint32_t bodyLoc = u->pItemData->nBodyLoc;
    if (bodyLoc >= imports::d2client::gaBodyClickTable->size()) {
        return ClickResult::InvalidTarget;
    }
    imports::d2client::gCursorHover->x = -1;
    imports::d2client::gCursorHover->y = -1;
    return GameThread::Execute([bodyLoc]() -> ClickResult {
        auto* player = imports::d2client::UNITS_GetPlayerUnit();
        if (player == nullptr || player->pInventory == nullptr) {
            return ClickResult::InvalidTarget;
        }
        auto& table = *imports::d2client::gaBodyClickTable;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bodyLoc bounds checked above
        auto click = table[bodyLoc];
        if (click == nullptr) {
            return ClickResult::InvalidTarget;
        }
        click(player, player->pInventory, static_cast<int32_t>(bodyLoc));
        return ClickResult::Dispatched;
    });
}

bool Unit::UseMenu(uint32_t menuId) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr) {
        return false;
    }
    auto* menu = imports::d2client::gNPCMenu.Ptr();
    if (menu == nullptr) {
        return false;
    }
    const uint32_t entryCount = *imports::d2client::gnNPCMenuAmount;
    const uint32_t targetClass = static_cast<uint32_t>(u->dwClassId);
    for (uint32_t i = 0; i < entryCount; ++i) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - menu table indexing
        auto* row = menu + i;
        if (row->dwNPCClassId != targetClass) {
            continue;
        }
        // Walk the 4 (entryId, entryFunc) slots looking for our menuId.
        // Slots are stored as 4 separate fields rather than arrays in
        // D2WinControlStrc to match binary layout; iteration uses a local
        // mapping. NPCMenu.h defines the underlying struct.
        const std::array<std::pair<uint16_t, decltype(row->pEntryFunc1)>, 4> entries = {
            std::pair{row->wEntryId1, row->pEntryFunc1},
            std::pair{row->wEntryId2, row->pEntryFunc2},
            std::pair{row->wEntryId3, row->pEntryFunc3},
            std::pair{row->wEntryId4, row->pEntryFunc4},
        };
        for (const auto& [entryId, entryFunc] : entries) {
            if (entryId == menuId && entryFunc != nullptr) {
                entryFunc();
                return true;
            }
        }
    }
    return false;
}

void Unit::Overhead(const std::string& text) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || text.empty()) {
        return;
    }
    auto ansi = d2bs::utils::ToStr(d2bs::utils::ToWStr(text), CP_ACP);
    auto* msg = imports::d2common::CHAT_AllocHoverMsg(nullptr, ansi.c_str(), *imports::d2client::gnOverheadTrigger);
    if (msg == nullptr) {
        return;
    }
    u->pHoverText = msg;
}

void Unit::Revive() const {
    D2GSPacketClt41 packet{};
    packet.nHeader = 0x41U;
    imports::d2net::CLIENT_Send(sizeof(packet), 1U, reinterpret_cast<uint8_t*>(&packet));
}

bool Unit::Shop(ShopMode mode) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_ITEM || u->pItemData == nullptr) {
        return false;
    }

    // Refuse if any transaction dialog is already in flight.
    if (*imports::d2client::gpTransactionDialog != nullptr || *imports::d2client::gnTransactionDialogs != 0U ||
        *imports::d2client::gnTransactionDialogs_2 != 0U) {
        return false;
    }

    // Reference JSUnit.cpp:1491-1494: ShopAction assumes the NPC shop UI window
    // has set up its render targets / hover items. Calling it outside that
    // lifecycle can corrupt transaction-dialog state.
    if (imports::d2client::UI_GetVar(UI_NPCSHOP) == 0) {
        return false;
    }

    auto* npc = imports::d2client::UI_GetInteractingNPC();
    if (npc == nullptr) {
        return false;
    }

    // Mode validation must match reference's accepted set: Sell(1), Buy(2),
    // BuyFill(6). Anything else short-circuits.
    if (mode != ShopMode::Sell && mode != ShopMode::Buy && mode != ShopMode::BuyFill) {
        return false;
    }

    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr) {
        return false;
    }
    auto* parentInv = u->pItemData->pExtraData.pParentInv;
    if (parentInv == nullptr || parentInv->pOwner == nullptr) {
        return false;
    }

    if (mode == ShopMode::Sell) {
        if (parentInv->pOwner->dwUnitId != player->dwUnitId) {
            return false;
        }
        imports::d2client::NPCS_ShopAction(npc, u, /*dwSell=*/1U, 0U, 0U, /*dwMode=*/1U, 1U, 0U);
        return true;
    }

    if (parentInv->pOwner->dwUnitId != npc->dwUnitId) {
        return false;
    }
    imports::d2client::NPCS_ShopAction(npc, u, /*dwSell=*/0U, 0U, 0U, /*dwMode=*/static_cast<uint32_t>(mode), 1U, 0U);
    return true;
}

bool Unit::SetSkill(uint16_t skillId, Hand hand, std::optional<uint32_t> itemId) const {
    // Hold the read lock across the pSkills linked-list walk below - without
    // it the chain could be mutated by the game thread between iterations.
    GameReadLock guard;
    if (AsUnit(ResolvePtr()) == nullptr) {
        return false;
    }
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr || player->pSkills == nullptr) {
        return false;
    }

    // pSkills can hold multiple entries for the same skill id - one from a
    // "+to skills" bonus and another from an item-charge. GetSkillLevel only
    // adds bonuses when nOwnerGUID == invalid, so the charge entry returns 0
    // while the +to-skills entry returns the real total. Stop on the first
    // entry that yields a non-zero level.
    uint32_t skillLevel = 0;
    for (auto* skill = player->pSkills->pFirstSkill; skill != nullptr; skill = skill->pNextSkill) {
        if (skill->pSkillsTxt == nullptr || static_cast<uint16_t>(skill->pSkillsTxt->nSkillId) != skillId) {
            continue;
        }
        skillLevel = imports::d2common::SKILLS_GetSkillLevel(player, skill, true);
        if (skillLevel != 0) {
            break;
        }
    }
    if (skillLevel == 0) {
        return false;
    }

    // 0x3C bind packet. D2MOO field names are slightly misleading:
    //   nMode high bit = hand selector (server reads bytes 1-4 as int32, sign bit = hand)
    //   dwFlags        = owner GUID (item id for charge skills, -1 otherwise)
    // Fire-and-forget: the V8 binding polls GetSkillId() for the confirmation.
    D2GSPacketClt3C packet{};
    packet.nHeader = 0x3CU;
    packet.nSkill = skillId;
    packet.nMode = (hand == Hand::Left) ? 0x8000U : 0U;
    packet.dwFlags = itemId.value_or(D2UnitInvalidGUID);
    imports::d2net::CLIENT_Send(sizeof(packet), 1U, reinterpret_cast<uint8_t*>(&packet));
    return true;
}

uint32_t Unit::GetMinionCount(uint32_t type) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || (u->dwUnitType != UNIT_MONSTER && u->dwUnitType != UNIT_PLAYER)) {
        return 0U;
    }
    return imports::d2client::UNITS_GetMinionCount(u, type);
}

uint32_t Unit::GetRepairCost(uint32_t npcClassId) const {
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr) {
        return 0U;
    }
    return imports::d2common::ITEMS_GetAllRepairCosts(nullptr, player, npcClassId,
                                                      static_cast<D2C_Difficulties>(GetDifficulty()),
                                                      *imports::d2client::gpItemPriceList, nullptr);
}

bool Unit::HasEnchant(uint32_t enchantId) const {
    auto* u = AsUnit(ResolvePtr());
    if (u == nullptr || u->dwUnitType != UNIT_MONSTER || u->pMonsterData == nullptr) {
        return false;
    }
    // Reference: 9 enchant slots in nMonUmod[0..8]. D2MOO declares the field 10
    // bytes wide for alignment; only the first 9 hold real data.
    constexpr size_t ENCHANT_SLOT_COUNT = 9;
    for (size_t i = 0; i < ENCHANT_SLOT_COUNT; ++i) {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
        if (u->pMonsterData->nMonUmod[i] == enchantId) {
            return true;
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    }
    return false;
}

// === Factory methods ===

Unit Unit::Player() {
    Unit unit;
    unit.kind_ = UnitKind::Player;
    return unit;
}

std::optional<Unit> Unit::Find(uint32_t id, std::optional<UnitType> type) {
    GameReadLock guard;
    if (type) {
        if (auto* u = FindUnitInHashTable(id, *type)) {
            return Unit::FromPtr(u);
        }
        return std::nullopt;
    }
    // No type constraint - try every type bucket in turn (0..5).
    for (uint32_t t = 0; t < UNIT_HASH_TYPE_COUNT; ++t) {
        if (auto* u = FindUnitInHashTable(id, static_cast<UnitType>(t))) {
            return Unit::FromPtr(u);
        }
    }
    return std::nullopt;
}

std::optional<Unit> Unit::CursorItem() {
    auto* p = imports::d2common::INVENTORY_GetCursorItem();
    if (p == nullptr) {
        return std::nullopt;
    }
    return Unit::FromPtr(p);
}

std::optional<Unit> Unit::Selected() {
    auto* p = imports::d2client::UNITS_GetSelectedUnit();
    if (p == nullptr) {
        return std::nullopt;
    }
    return Unit::FromPtr(p);
}

std::optional<Unit> Unit::SelectedInventoryItem() {
    auto* p = *imports::d2client::gpSelectedInvItem;
    if (p == nullptr) {
        return std::nullopt;
    }
    // Reference (JSUnit.cpp:624) stamps PRIVATE_UNIT regardless of which
    // fallback resolved the pointer - the cursor has no owner anchor, so
    // routing it through FindNextInventoryItem would gate on the missing
    // anchor and return nullopt forever. Leave kind_ as Regular so getNext()
    // walks the regular hash table from the resolved unit.
    return Unit::FromPtr(p);
}

std::optional<Unit> Unit::InteractingNPC() {
    auto* p = imports::d2client::UI_GetInteractingNPC();
    if (p == nullptr) {
        return std::nullopt;
    }
    return Unit::FromPtr(p);
}

}  // namespace d2bs::game
