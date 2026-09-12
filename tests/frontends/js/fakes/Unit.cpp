#include "game/Unit.h"

#include "fakes/GameLoopCollaborators.h"
#include "fakes/UnitStore.h"

// Unit fake. The player - id 0, the default-constructed handle - reads GameLoop test
// state exactly as before. Items are handles into the UnitStore, minted by FromPtr with
// the id in place of a pointer. Only the members GameLoop::TakeSnapshot, UnitToJson and
// the inventory walk call are implemented; the rest stay declaration-only and unlinked.

namespace d2bs::game {

namespace {

const test::FakeItem* ItemById(uint32_t id) {
    return test::Units().Find(id);
}

Unit ItemHandle(uint32_t id) {
    return Unit::FromPtr(reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
}

}  // namespace

// Resolvable handle == truthy, as in the real contract. The pointer is never
// dereferenced - accessors read the fakes.
void* Unit::ResolvePtr() const {
    if (type_ == UnitType::Item) {
        return ItemById(unitId_) != nullptr ? reinterpret_cast<void*>(static_cast<uintptr_t>(unitId_)) : nullptr;
    }
    if (!test::State().playerId.has_value()) {
        return nullptr;
    }
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x1));
}

Unit Unit::FromPtr(void* p) {
    Unit unit;
    unit.unitId_ = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
    unit.type_ = UnitType::Item;
    return unit;
}

Unit Unit::Player() {
    return Unit{};
}

UnitType Unit::Type() const {
    return type_;
}

uint32_t Unit::Id() const {
    if (type_ == UnitType::Item) {
        return unitId_;
    }
    return test::State().playerId.value_or(0);
}

uint32_t Unit::ClassId() const {
    if (const auto* item = ItemById(unitId_); type_ == UnitType::Item && item != nullptr) {
        return item->classId;
    }
    return test::Units().player.classId;
}

uint32_t Unit::Hp() const {
    return test::State().hp.value_or(0);
}

uint32_t Unit::Mp() const {
    return test::State().mp.value_or(0);
}

uint32_t Unit::Area() const {
    return test::State().areaId;
}

// === Wearer ===

uint32_t Unit::FlagsEx() const {
    return test::Units().player.flagsEx;
}

uint32_t Unit::WeaponSwitch() const {
    return test::Units().player.weaponSwitch;
}

std::string Unit::Name() const {
    if (const auto* item = ItemById(unitId_); type_ == UnitType::Item && item != nullptr) {
        return item->name;
    }
    return test::Units().player.name;
}

int32_t Unit::GetStat(uint32_t stat, uint32_t /*sub*/) const {
    const auto& stats = test::Units().player.stats;
    const auto it = stats.find(stat);
    return it == stats.end() ? 0 : it->second;
}

std::vector<Unit::SkillInfo> Unit::GetAllSkills() const {
    return test::Units().player.skills;
}

// === Item ===

std::string Unit::ItemCode() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->code : std::string{};
}

ItemQuality Unit::Quality() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->quality : ItemQuality::Normal;
}

uint32_t Unit::ItemFlags() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->itemFlags : 0;
}

uint16_t Unit::ItemFormat() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->format : 0;
}

std::optional<uint32_t> Unit::FileIndex() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->fileIndex : std::nullopt;
}

uint16_t Unit::RarePrefixNum() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->rarePrefix : 0;
}

uint16_t Unit::RareSuffixNum() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->rareSuffix : 0;
}

uint16_t Unit::AutoAffixNum() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->autoAffix : 0;
}

std::array<uint16_t, Unit::MAX_AFFIX_SLOTS> Unit::PrefixNums() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->prefixes : std::array<uint16_t, MAX_AFFIX_SLOTS>{};
}

std::array<uint16_t, Unit::MAX_AFFIX_SLOTS> Unit::SuffixNums() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->suffixes : std::array<uint16_t, MAX_AFFIX_SLOTS>{};
}

uint32_t Unit::ItemLevel() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->itemLevel : 0;
}

uint32_t Unit::EarLevel() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->earLevel : 0;
}

std::string Unit::ItemPlayerName() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->playerName : std::string{};
}

uint32_t Unit::GfxIndex() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->gfxIndex : 0;
}

std::string Unit::Description() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->description : std::string{};
}

std::vector<StatListEntry> Unit::GetStatLists() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->statLists : std::vector<StatListEntry>{};
}

ItemLocation Unit::ItemLocation() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->location : game::ItemLocation::Null;
}

Position Unit::Pos() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->pos : Position{};
}

Size Unit::Size() const {
    const auto* item = ItemById(unitId_);
    return item != nullptr ? item->size : game::Size{};
}

// === Traversal ===

std::optional<Unit> Unit::GetFirstItem() const {
    const auto& store = test::Units();
    uint32_t first = 0;
    if (type_ == UnitType::Item) {
        if (const auto* item = store.Find(unitId_); item != nullptr && !item->children.empty()) {
            first = item->children.front();
        }
    } else if (!store.player.items.empty()) {
        first = store.player.items.front();
    }
    if (first == 0) {
        return std::nullopt;
    }
    return ItemHandle(first);
}

std::optional<Unit> Unit::GetNextItem() const {
    const auto* item = type_ == UnitType::Item ? ItemById(unitId_) : nullptr;
    if (item == nullptr || item->next == 0) {
        return std::nullopt;
    }
    return ItemHandle(item->next);
}

}  // namespace d2bs::game
