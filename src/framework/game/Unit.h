#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "GameHelpers.h"
#include "game/HandleCache.h"
#include "game/Types.h"

namespace d2bs::game {

class Room;

// Identity-based handle for D2UnitStrc*.
// Stores (unitId, type, kind) as identity and resolves to a game pointer on demand
// with per-frame caching via HandleCache.
class Unit {
    uint32_t unitId_ = 0;
    UnitType type_ = UnitType::Player;
    UnitKind kind_ = UnitKind::Regular;
    HandleCache cache_;

    // Iteration cursor state (mutable, used by FindNext / FindNextInventoryItem).
    // Carries search domain, match criteria, and (for InventoryItem cursors) owner anchor.
    UnitCursorState cursor_;

    void* ResolvePtr() const;

   public:
    Unit() = default;
    explicit operator bool() const { return ResolvePtr() != nullptr; }
    bool operator==(const Unit& other) const;

    // Extracts unitId/type from a raw D2UnitStrc pointer.
    static Unit FromPtr(void* p);

    // === Direct struct reads ===
    UnitType Type() const;
    uint32_t ClassId() const;
    uint32_t Mode() const;
    uint32_t Id() const;
    uint32_t Act() const;

    // === Position ===
    // Pos()/TargetPos() are in game coordinates (same convention as Room/Level
    // getters - see docs/coords.md).
    Position Pos() const;
    Position TargetPos() const;
    uint32_t Area() const;

    // === Stats ===
    uint32_t Hp() const;
    uint32_t HpMax() const;
    uint32_t Mp() const;
    uint32_t MpMax() const;
    uint32_t Stamina() const;
    uint32_t StaminaMax() const;
    uint32_t CharLevel() const;
    int32_t GetStat(uint32_t stat, uint32_t sub = 0) const;
    bool HasState(uint32_t stateId) const;

    std::vector<StatEntry> GetAllStats() const;
    // Returns detailed stat list for getStat(-2): includes both base stats and stat-list stats.
    // Reference: InsertStatsToGenericObject iterates pUnit->pStats and D2COMMON_GetStatList(pUnit, NULL, 0x40).
    std::vector<StatEntry> GetDetailedStats() const;

    // === Name ===
    std::string Name() const;
    std::string ItemFullName() const;

    // === Unit info ===
    uint32_t Direction() const;
    std::optional<uint32_t> UniqueId() const;
    uint32_t SpecType() const;
    uint32_t ItemCount() const;

    // Resolves the owning unit. Game-impl dispatches three ways on Type():
    //   Monster -> summoner via fn::GetMonsterOwner(unitId)
    //   Missile -> caster via fn::GetMissileOwnerUnit(pMissile)
    //   Item    -> pItemData->pOwnerInventory->pOwner
    //   other   -> nullopt
    // Returns a fully resolved Unit handle or nullopt when no owner exists.
    std::optional<Unit> GetOwner() const;

    // === Item-specific ===
    std::string ItemCode() const;
    std::string Prefix() const;
    std::string Suffix() const;
    uint16_t PrefixNum() const;
    uint16_t SuffixNum() const;
    // Item prefix/suffix arrays - fixed-size 3 slots matching D2's
    // `wMagicPrefix[3]` / `wMagicSuffix[3]` (D2MOO `ITEMS_MAX_MODS == 3`).
    // Empty/zero entries are preserved as nullopt / 0 so JS scripts can
    // index by slot - reference parity, JSUnit.cpp:341-394 emits sparse
    // arrays with `JS_SetElement` only on non-zero codes.
    static constexpr size_t MAX_AFFIX_SLOTS = 3;
    std::array<std::optional<std::string>, MAX_AFFIX_SLOTS> Prefixes() const;
    std::array<std::optional<std::string>, MAX_AFFIX_SLOTS> Suffixes() const;
    std::array<uint16_t, MAX_AFFIX_SLOTS> PrefixNums() const;
    std::array<uint16_t, MAX_AFFIX_SLOTS> SuffixNums() const;
    ItemQuality Quality() const;
    NodePage Node() const;
    ItemLocation ItemLocation() const;
    // Item grid footprint (inventory xSize/ySize from item txt data).
    Size Size() const;
    uint32_t ItemType() const;
    std::string Description() const;
    BodyLocation BodyLocation() const;
    uint32_t ItemLevel() const;
    uint32_t LevelRequirement() const;
    uint32_t GfxIndex() const;
    uint32_t ItemFlags() const;
    uint32_t ItemCost(ItemCostMode mode, uint32_t npcClassId, Difficulty difficulty) const;

    // === Object-specific ===
    uint32_t ObjType() const;
    bool IsLocked() const;

    // === Player-specific ===
    uint32_t RunWalk() const;
    uint32_t WeaponSwitch() const;

    // === Traversal ===
    // Each primitive returns nullopt when the chain is empty/exhausted, so the
    // standard loop pattern is
    //   for (auto u = First(); u; u = u->Next()) { ... }
    // where both sides are `std::optional<Unit>`.
    std::optional<Unit> GetFirstItem() const;
    std::optional<Unit> GetNextItem() const;
    // First unit of `type` in that hash table (walks buckets for first non-null).
    static std::optional<Unit> GetFirstInGame(UnitType type);
    // Next unit along the hash-chain (`pUnit->pListNext`). Distinct from pRoomNext.
    std::optional<Unit> GetNextInGame() const;
    std::string GetParentName() const;  // For objects: returns pObjectData->szOwner
    Room GetRoom() const;

    // === Framework-impl (defined inline in Finders.h) ===
    // Unified hash-table search. Reference parallel: GetUnit(name, classId, type, mode, unitId).
    static std::optional<Unit> FindFirst(const UnitCursorState& state);
    // Find next unit matching stored cursor. Returns new handle pointing to next match.
    std::optional<Unit> FindNext() const;
    // Walk this unit's inventory and return the first item matching `state`
    // (name / classId / mode / unitId, all optional). The returned handle is
    // stamped with kind=InventoryItem, owner = *this, and the cursor state so
    // FindNextInventoryItem() can resume with the same criteria. Reference
    // parallel: GetInvUnit(pOwner, ...).
    std::optional<Unit> FindFirstInventoryItem(const UnitCursorState& state = {}) const;
    // Find next inventory item matching stored cursor + owner. Returns new handle.
    std::optional<Unit> FindNextInventoryItem() const;
    // Walk current act's Room1 chain searching for this unit's merc (summoned
    // monster whose owner id == this unit's id). Currently stubbed: real
    // implementation deferred until Act/Room1 traversal primitives land.
    std::optional<Unit> FindMerc() const;
    // Bulk collect of this unit's inventory items via GetFirstItem/GetNextItem.
    // Empty vector on no inventory (binding emits JS `undefined` in that case).
    std::vector<Unit> GetItems() const;

    // === Cursor access (for getNext() to update iteration state from JS args) ===
    UnitCursorState& Cursor() { return cursor_; }
    const UnitCursorState& Cursor() const { return cursor_; }

    // === Kind/identity access (read-only - set at construction via factory methods) ===
    UnitKind Kind() const { return kind_; }

    // === Skill methods ===
    struct SkillInfo {
        uint16_t skillId;
        uint32_t baseLevel;
        uint32_t totalLevel;
    };
    std::string GetSkillName(Hand hand) const;
    uint16_t GetSkillId(Hand hand) const;
    std::vector<SkillInfo> GetAllSkills() const;
    // Charge filter:
    //   nullopt -> non-charge skills only (IsCharge == 0).
    //   false   -> no filter (match any skill).
    //   true    -> charge-only skills (IsCharge == 1).
    std::optional<uint32_t> GetSkillLevel(uint16_t skillId, bool includeExtraLevels,
                                          std::optional<bool> charge = std::nullopt) const;

    // === Actions ===
    void Move(Position target) const;
    bool Interact() const;
    // Returns true on dispatch, false when waypointId is not a valid waypoint
    // (levels.txt[wpId].Waypoint == 255) or the unit can't be resolved.
    bool TakeWaypoint(uint32_t waypointId) const;

    void Repair() const;
    bool UseMenu(uint32_t menuId) const;
    ClickResult EquipItem() const;
    void Overhead(const std::string& text) const;
    void Revive() const;
    bool Shop(ShopMode mode) const;
    bool SetSkill(uint16_t skillId, Hand hand, std::optional<uint32_t> itemId) const;
    uint32_t GetMinionCount(uint32_t type) const;
    uint32_t GetRepairCost(uint32_t npcClassId) const;
    bool HasEnchant(uint32_t enchantId) const;

    // === Factory methods ===
    static Unit Player();
    static std::optional<Unit> Find(uint32_t id, std::optional<UnitType> type = std::nullopt);
    static std::optional<Unit> CursorItem();
    static std::optional<Unit> Selected();
    static std::optional<Unit> SelectedInventoryItem();
    static std::optional<Unit> InteractingNPC();
};

}  // namespace d2bs::game
