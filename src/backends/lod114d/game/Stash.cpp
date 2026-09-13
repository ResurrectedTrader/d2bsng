#include "game/StashTab.h"

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

// Without PlugY pages 1.14d has one stash: a single personal tab.
StashTab::operator bool() const {
    GameReadLock guard;
    if (plugy::HasPages()) {
        return plugy::HasPage(kind_, index_);
    }
    return kind_ == StashTabKind::Personal && index_ == 0 && Unit::Player();
}

StashTabType StashTab::Type() const {
    return StashTabType::Normal;
}

std::string StashTab::Name() const {
    GameReadLock guard;
    return plugy::HasPages() ? plugy::PageName(kind_, index_) : std::string{};
}

// Neither stash keeps gold per page: the character's stash gold sits on personal
// tab 0 and PlugY's shared pool on shared tab 0.
uint32_t StashTab::Gold() const {
    GameReadLock guard;
    if (index_ != 0 || !*this) {
        return 0;
    }
    if (kind_ == StashTabKind::Shared) {
        return plugy::SharedGold();
    }
    const auto gold = Unit::Player().GetStat(STAT_GOLDBANK);
    return gold > 0 ? static_cast<uint32_t>(gold) : 0U;
}

std::vector<Unit> StashTab::GetItems() const {
    GameReadLock guard;
    if (plugy::HasPages()) {
        return plugy::GetPageItems(kind_, index_);
    }
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
    const auto click = [cell] {
        return ClickContainerSlot(ClickButton::Left, cell, ItemLocation::Stash);
    };
    if (plugy::HasPages()) {
        return plugy::WithActivePage(kind_, index_, click);
    }
    return *this ? click() : ClickResult::StashTabUnavailable;
}

bool StashTab::DepositGold(uint32_t amount) const {
    if (kind_ == StashTabKind::Shared) {
        return index_ == 0 && plugy::HasPages() && plugy::MoveSharedGold(GoldActionMode::Deposit);
    }
    if (index_ != 0 || amount == 0 || !*this) {
        return false;
    }
    GoldAction(GoldActionMode::Deposit, static_cast<int32_t>(std::min<uint32_t>(amount, INT32_MAX)));
    return true;
}

bool StashTab::WithdrawGold(uint32_t amount) const {
    if (kind_ == StashTabKind::Shared) {
        return index_ == 0 && plugy::HasPages() && plugy::MoveSharedGold(GoldActionMode::Withdraw);
    }
    if (index_ != 0 || amount == 0 || !*this) {
        return false;
    }
    GoldAction(GoldActionMode::Withdraw, static_cast<int32_t>(std::min<uint32_t>(amount, INT32_MAX)));
    return true;
}

std::vector<StashTab> GetStashTabs() {
    GameReadLock guard;
    std::vector<StashTab> tabs;
    if (plugy::HasPages()) {
        for (const auto kind : {StashTabKind::Personal, StashTabKind::Shared}) {
            for (uint32_t index = 0, count = plugy::PageCount(kind); index < count; ++index) {
                tabs.emplace_back(kind, index);
            }
        }
    } else if (Unit::Player()) {
        tabs.emplace_back(StashTabKind::Personal, 0);
    }
    return tabs;
}

std::optional<StashTab> Unit::StashTab() const {
    GameReadLock guard;
    if (Type() != UnitType::Item) {
        return std::nullopt;
    }
    if (plugy::HasPages()) {
        return plugy::FindPage(*this);
    }
    if (ItemLocation() != ItemLocation::Stash) {
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
