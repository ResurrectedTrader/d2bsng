#include "PlugY.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/Unit.h"

#include <D2StatList.h>  // STAT_GOLDBANK

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace d2bs::game {

namespace {

// Vanilla LoD: the whole stash is one personal tab, always active, holding the
// character's stash gold.
StashTab VanillaTab() {
    const auto gold = Unit::Player().GetStat(STAT_GOLDBANK);
    return {.kind = StashTabKind::Personal,
            .index = 0,
            .type = StashTabType::Normal,
            .name = {},
            .isActive = true,
            .gold = gold > 0 ? static_cast<uint32_t>(gold) : 0U};
}

bool IsVanillaTab(StashTabKind kind, uint32_t index) {
    return kind == StashTabKind::Personal && index == 0;
}

}  // namespace

std::vector<StashTab> GetStashTabs() {
    GameReadLock guard;
    if (plugy::HasPages()) {
        return plugy::GetStashTabs();
    }
    if (!Unit::Player()) {
        return {};
    }
    return {VanillaTab()};
}

std::vector<Unit> GetStashTabItems(StashTabKind kind, uint32_t index) {
    GameReadLock guard;
    if (plugy::HasPages()) {
        return plugy::GetStashTabItems(kind, index);
    }
    std::vector<Unit> items;
    if (!IsVanillaTab(kind, index)) {
        return items;
    }
    for (auto item = Unit::Player().GetFirstItem(); item; item = item->GetNextItem()) {
        if (item->ItemLocation() == ItemLocation::Stash) {
            items.push_back(*item);
        }
    }
    return items;
}

ClickResult ClickStashTabSlot(StashTabKind kind, uint32_t index, Position gridPos) {
    const bool hasPages = [] {
        GameReadLock guard;
        return plugy::HasPages();
    }();
    if (hasPages) {
        return plugy::WithActivePage(
            kind, index, [gridPos] { return ClickContainerSlot(ClickButton::Left, gridPos, ItemLocation::Stash); });
    }
    if (!IsVanillaTab(kind, index)) {
        return ClickResult::StashTabUnavailable;
    }
    return ClickContainerSlot(ClickButton::Left, gridPos, ItemLocation::Stash);
}

bool StashTabGold(StashTabKind kind, uint32_t index, GoldActionMode mode, uint32_t amount) {
    if (mode != GoldActionMode::Deposit && mode != GoldActionMode::Withdraw) {
        return false;
    }
    const bool hasPages = [] {
        GameReadLock guard;
        return plugy::HasPages();
    }();
    if (kind == StashTabKind::Shared) {
        return hasPages && index == 0 && plugy::MoveSharedGold(mode);
    }
    // Personal gold is the character's stash gold whichever page is showing.
    if (index != 0 || amount == 0) {
        return false;
    }
    if (!hasPages && !Unit::Player()) {
        return false;
    }
    GoldAction(mode, static_cast<int32_t>(std::min<uint32_t>(amount, INT32_MAX)));
    return true;
}

std::optional<StashTab> Unit::StashTab() const {
    GameReadLock guard;
    if (Type() != UnitType::Item) {
        return std::nullopt;
    }
    if (plugy::HasPages()) {
        return plugy::FindStashTab(*this);
    }
    if (ItemLocation() != ItemLocation::Stash) {
        return std::nullopt;
    }
    // Only the local player has a stash, so a stash-located item is on its one tab
    // as long as it hangs off the player's inventory.
    for (auto item = Unit::Player().GetFirstItem(); item; item = item->GetNextItem()) {
        if (*item == *this) {
            return VanillaTab();
        }
    }
    return std::nullopt;
}

}  // namespace d2bs::game
