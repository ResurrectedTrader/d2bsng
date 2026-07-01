#pragma once

// Framework-owned finder implementations: filter/walk logic composed from game-layer primitives, shared across every
// port. Never touches game memory directly.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "game/Bridge.h"
#include "game/Constants.h"
#include "game/Control.h"
#include "game/Level.h"
#include "game/Party.h"
#include "game/Room.h"
#include "game/Types.h"
#include "game/Unit.h"
#include "utils/utils.h"

namespace d2bs::game {

// === Internal helpers ===

inline Unit Stamped(Unit u, const UnitCursorState& s) {
    u.Cursor() = s;
    return u;
}

// Reference parity target: CheckUnit at reference/d2bs/Unit.cpp:238-272.
//
// Match predicate used by every unit-filter walk (hash and inventory). Type
// constraint is NOT checked here: the hash-table walk guarantees every
// candidate already has the right type, and inventory walks are always over
// items. Keeping type out of the predicate avoids redundant work and keeps
// behaviour exactly in line with the reference.
inline bool Matches(const Unit& u, const UnitCursorState& s) {
    if (s.unitId && u.Id() != *s.unitId)
        return false;
    if (s.classId && u.ClassId() != *s.classId)
        return false;

    if (s.mode) {
        const uint32_t m = *s.mode;
        if (m >= ITEM_LOCATION_MODE_OFFSET && u.Type() == UnitType::Item) {
            // Items: mode >= 100 encodes an ItemLocation filter.
            auto loc = static_cast<uint32_t>(u.ItemLocation());
            if (loc != m - ITEM_LOCATION_MODE_OFFSET)
                return false;
        } else if ((m & (1U << 29U)) != 0) {
            // Bitmask mode: any bit 0..27 of m that matches pUnit->dwMode.
            bool any = false;
            for (uint32_t i = 0; i < 28; ++i) {
                if ((m & (1U << i)) != 0 && u.Mode() == i) {
                    any = true;
                    break;
                }
            }
            if (!any)
                return false;
        } else if (u.Mode() != m) {
            return false;
        }
    }

    if (s.name && !s.name->empty()) {
        std::string candidate = (u.Type() == UnitType::Item) ? u.ItemCode() : u.Name();
        if (!utils::EqualsCaseInsensitive(candidate, *s.name))
            return false;
    }

    return true;
}

inline constexpr std::array ALL_UNIT_TYPES = {
    UnitType::Player, UnitType::Monster, UnitType::Object, UnitType::Missile, UnitType::Item, UnitType::Tile,
};

inline std::optional<UnitType> NextTypeAfter(UnitType t) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - linear scan of fixed table
    for (size_t i = 0; i + 1 < ALL_UNIT_TYPES.size(); ++i) {
        if (ALL_UNIT_TYPES[i] == t)
            return ALL_UNIT_TYPES[i + 1];
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return std::nullopt;
}

// === Unit finders ===

inline std::optional<Unit> Unit::FindFirst(const UnitCursorState& s) {
    if (s.unitId) {
        auto u = Find(*s.unitId, s.type);
        if (u && Matches(*u, s))
            return Stamped(*u, s);
        return std::nullopt;
    }

    if (s.type) {
        for (auto u = GetFirstInGame(*s.type); u; u = u->GetNextInGame()) {
            if (Matches(*u, s))
                return Stamped(*u, s);
        }
        return std::nullopt;
    }

    for (auto t : ALL_UNIT_TYPES) {
        for (auto u = GetFirstInGame(t); u; u = u->GetNextInGame()) {
            if (Matches(*u, s))
                return Stamped(*u, s);
        }
    }
    return std::nullopt;
}

inline std::optional<Unit> Unit::FindNext() const {
    const auto& s = Cursor();

    // Advance within the current hash table.
    for (auto u = GetNextInGame(); u; u = u->GetNextInGame()) {
        if (Matches(*u, s))
            return Stamped(*u, s);
    }

    if (s.type)
        return std::nullopt;

    for (auto t = NextTypeAfter(Type()); t; t = NextTypeAfter(*t)) {
        for (auto u = GetFirstInGame(*t); u; u = u->GetNextInGame()) {
            if (Matches(*u, s))
                return Stamped(*u, s);
        }
    }
    return std::nullopt;
}

inline std::optional<Unit> Unit::FindFirstInventoryItem(const UnitCursorState& state) const {
    // Walk this unit's inventory; stamp cursor with this unit as anchor.
    for (auto item = GetFirstItem(); item; item = item->GetNextItem()) {
        if (Matches(*item, state)) {
            Unit u = *item;
            u.Cursor() = state;
            u.Cursor().ownerId = Id();
            u.Cursor().ownerType = Type();
            return u;
        }
    }
    return std::nullopt;
}

inline std::optional<Unit> Unit::FindNextInventoryItem() const {
    const auto& s = Cursor();
    if (!s.ownerId || !s.ownerType)
        return std::nullopt;  // Not an InventoryItem cursor.

    auto anchor = Find(*s.ownerId, *s.ownerType);
    if (!anchor)
        return std::nullopt;

    // Reference parity: validate the item is still in the original inventory.
    // GetOwner() for items walks pItemData->pOwnerInventory->pOwner, so an
    // identity compare to the stored anchor is behaviourally equivalent to
    // reference's `pItem->pItemData->pOwnerInventory == pOwner->pInventory`.
    auto liveOwner = GetOwner();
    if (!liveOwner || !(*liveOwner == *anchor))
        return std::nullopt;

    for (auto next = GetNextItem(); next; next = next->GetNextItem()) {
        if (Matches(*next, s))
            return Stamped(*next, s);
    }
    return std::nullopt;
}

inline std::optional<Unit> Unit::FindMerc() const {
    // Reference D2Helpers.cpp:482-498 walks pUnit->pAct->pRoom1 then each
    // Room1's pUnitFirst/pRoomNext chain. We walk the monster hash table
    // directly via existing iteration primitives - the set of loaded monsters
    // is equivalent (D2 only loads monsters for the current act), and this
    // avoids introducing Act-level Room1 traversal primitives just for this
    // one method. Filter: dwType==Monster, dwTxtFileNo in {MERC_A1..A5},
    // GetMonsterOwner(merc) resolves to the caller.
    for (auto m = GetFirstInGame(UnitType::Monster); m; m = m->GetNextInGame()) {
        const auto cls = m->ClassId();
        if (cls != MERC_CLASS_ID_ACT1 && cls != MERC_CLASS_ID_ACT2 && cls != MERC_CLASS_ID_ACT3 &&
            cls != MERC_CLASS_ID_ACT5)
            continue;
        auto owner = m->GetOwner();  // Monster branch -> fn::GetMonsterOwner
        if (owner && *owner == *this)
            return m;
    }
    return std::nullopt;
}

// Bulk-collect inventory items via GetFirstItem / GetNextItem. Empty vector
// when the unit has no inventory or an empty one - binding emits JS
// `undefined` on .empty() (matches reference behaviour).
inline std::vector<Unit> Unit::GetItems() const {
    std::vector<Unit> out;
    for (auto item = GetFirstItem(); item; item = item->GetNextItem()) {
        out.push_back(*item);
    }
    return out;
}

// === Level finders ===

// Reference parallel: JSRoom.cpp:544 uses half-open bounds
//   x >= pos.x && y >= pos.y && x < pos.x+sizeX && y < pos.y+sizeY
// Rect::Contains uses the same half-open convention.
inline std::optional<Room> Level::FindRoomAt(Position pos) const {
    for (auto r = GetFirstRoom(); r; r = r.GetNext()) {
        if (r.Bounds().Contains(pos))
            return r;
    }
    return std::nullopt;
}

inline std::vector<PresetUnitInfo> Level::GetPresetUnits(std::optional<uint32_t> type,
                                                         std::optional<uint32_t> classId) const {
    std::vector<PresetUnitInfo> out;
    const uint32_t levelId = Id();
    for (auto r = GetFirstRoom(); r; r = r.GetNext()) {
        auto roomPresets = r.GetPresetUnits(type, classId);
        for (auto& pu : roomPresets) {
            pu.level = levelId;
        }
        out.insert(out.end(), roomPresets.begin(), roomPresets.end());
    }
    return out;
}

// === Party finders (static factories) ===

// Reference: JSParty.cpp:127 - exact GID match on the roster chain.
inline std::optional<Party> Party::FindById(uint32_t id) {
    for (auto p = GetFirst(); p; p = std::optional{p->GetNext()}) {
        if (!*p)
            break;
        if (p->Id() == id)
            return p;
    }
    return std::nullopt;
}

// Reference: JSParty.cpp:132 - case-insensitive ASCII compare (`_stricmp`).
inline std::optional<Party> Party::FindByName(const std::string& name) {
    for (auto p = GetFirst(); p; p = std::optional{p->GetNext()}) {
        if (!*p)
            break;
        if (utils::EqualsCaseInsensitive(p->Name(), name))
            return p;
    }
    return std::nullopt;
}

// === Control finders (static factory) ===

// Reference: Control.cpp:24-120 findControl.
// Precedence/short-circuit order (JS-observable):
//   1. Menu state (gated at the binding level / WaitForGameReady).
//   2. All-nullopt shortcut -> first control.
//   3. Per-control loop, per-axis filter: Type, PosX, PosY, SizeX, SizeY.
// Text / Disabled constraints from reference are not exposed via our JS API.
inline std::optional<Control> Control::Find(std::optional<ControlType> type, std::optional<uint32_t> x,
                                            std::optional<uint32_t> y, std::optional<uint32_t> xsize,
                                            std::optional<uint32_t> ysize, std::optional<int32_t> localeId) {
    // One read lock for the whole walk: inner ResolvePtr() locks collapse to
    // free recursive re-entries, and the control list can't shift mid-iteration.
    auto guard = Bridge::Lock();
    if (!type && !x && !y && !xsize && !ysize && !localeId) {
        return GetFirst();
    }
    for (auto c = GetFirst(); c; c = std::optional{c->GetNext()}) {
        if (!*c)
            break;
        if (type && c->Type() != *type)
            continue;
        auto cb = c->Bounds();
        if (x && cb.origin.x != *x)
            continue;
        if (y && cb.origin.y != *y)
            continue;
        if (xsize && cb.size.width != *xsize)
            continue;
        if (ysize && cb.size.height != *ysize)
            continue;
        if (localeId && !c->HasLocaleText(*localeId))
            continue;
        return c;
    }
    return std::nullopt;
}

}  // namespace d2bs::game
