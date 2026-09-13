#include "game/StashTab.h"

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

// 1.14d has one stash: a single personal tab.
bool IsTheTab(const StashTab& tab) {
    return tab.Kind() == StashTabKind::Personal && tab.Index() == 0;
}

// The character's stash gold, one figure for the whole stash.
uint32_t BankGold() {
    const auto gold = Unit::Player().GetStat(STAT_GOLDBANK);
    return gold > 0 ? static_cast<uint32_t>(gold) : 0U;
}

bool MoveGold(const StashTab& tab, GoldActionMode mode, uint32_t amount) {
    GameReadLock guard;
    if (!IsTheTab(tab) || amount == 0 || !Unit::Player()) {
        return false;
    }
    GoldAction(mode, static_cast<int32_t>(std::min<uint32_t>(amount, INT32_MAX)));
    return true;
}

}  // namespace

StashTab::operator bool() const {
    GameReadLock guard;
    return IsTheTab(*this) && Unit::Player();
}

StashTabType StashTab::Type() const {
    return StashTabType::Normal;
}

std::string StashTab::Name() const {
    return {};
}

uint32_t StashTab::Gold() const {
    GameReadLock guard;
    return *this ? BankGold() : 0U;
}

std::vector<Unit> StashTab::GetItems() const {
    GameReadLock guard;
    std::vector<Unit> items;
    if (!*this) {
        return items;
    }
    for (auto item = Unit::Player().GetFirstItem(); item; item = item->GetNextItem()) {
        if (item->ItemLocation() == ItemLocation::Stash) {
            items.push_back(*item);
        }
    }
    return items;
}

ClickResult StashTab::Click(Position cell) const {
    return IsTheTab(*this) ? ClickContainerSlot(ClickButton::Left, cell, ItemLocation::Stash)
                           : ClickResult::StashTabUnavailable;
}

bool StashTab::DepositGold(uint32_t amount) const {
    return MoveGold(*this, GoldActionMode::Deposit, amount);
}

bool StashTab::WithdrawGold(uint32_t amount) const {
    return MoveGold(*this, GoldActionMode::Withdraw, amount);
}

std::vector<StashTab> GetStashTabs() {
    GameReadLock guard;
    std::vector<StashTab> tabs;
    if (Unit::Player()) {
        tabs.emplace_back(StashTabKind::Personal, 0);
    }
    return tabs;
}

std::optional<StashTab> Unit::StashTab() const {
    GameReadLock guard;
    if (Type() != UnitType::Item || ItemLocation() != ItemLocation::Stash) {
        return std::nullopt;
    }
    // Only the local player has a stash, so a stash-located item is on its one tab
    // as long as it hangs off the player's inventory.
    for (auto item = Unit::Player().GetFirstItem(); item; item = item->GetNextItem()) {
        if (*item == *this) {
            return game::StashTab(StashTabKind::Personal, 0);
        }
    }
    return std::nullopt;
}

}  // namespace d2bs::game
