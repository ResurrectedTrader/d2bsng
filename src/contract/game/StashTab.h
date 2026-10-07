#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/Types.h"

namespace d2bs::game {

class Unit;

// Identity handle for one tab of the local player's stash: its kind and its
// position within that kind. Every read resolves live through the backend, so a
// handle stays valid across page changes and reads empty once its tab is gone.
// Vanilla LoD has exactly one tab, Personal 0; paged stashes (mods) and D2R add
// more, including account-wide shared tabs.
class StashTab {
   public:
    StashTab(StashTabKind kind, uint32_t index) : kind_(kind), index_(index) {}

    StashTabKind Kind() const { return kind_; }
    uint32_t Index() const { return index_; }

    // === Game-impl required (src/backends/<port>/game/Stash.cpp) ===

    // The tab exists right now (there is a player unit and the tab is present).
    explicit operator bool() const;
    StashTabType Type() const;
    // User-given name; empty when unnamed.
    std::string Name() const;
    // Gold stored on the tab. Where gold is not tracked per tab it is attributed to
    // the first tab of its kind (the character's stash gold to Personal 0, a shared
    // pool to Shared 0) and every other tab reads 0.
    uint32_t Gold() const;
    // The tab's items, shown or not. A backend that parks unshown tabs outside the
    // inventory still keeps those units alive, so they resolve as regular handles.
    std::vector<Unit> GetItems() const;
    // Left-click a grid cell: picks up the item there, drops the cursor item, or
    // swaps, exactly as ClickContainerSlot(Left, cell, Stash) does on the shown tab.
    // The stash panel must be open. A backend whose packets cannot address an
    // unshown tab blocks the calling script thread while the tab is brought in
    // and clicked, then best-effort restores the previously shown tab (which tab
    // ends up shown is not part of the contract). StashTabUnavailable for a tab
    // that does not exist or could not be brought in.
    // An advanced stash tab has no grid to drop on: with an item on the cursor
    // the click deposits it (Deposit) wherever `cell` is, otherwise it withdraws
    // one of the stack at `cell` to the cursor (Withdraw). InvalidTarget when
    // that is not possible.
    ClickResult Click(Position cell) const;
    // Take `count` items off `item`, one of the tab's items, and put them at
    // `target`. On an advanced stash tab (Type() == AdvancedStash) `item` is a
    // stack, and the server stops early when it runs out or the target is full.
    // On a plain grid tab only a count of 1 to the cursor is possible: the item
    // is picked up, which needs an empty cursor; not every backend offers it. The
    // cursor holds one item, so it takes a count of 1 only. The stash panel must
    // be open. Fire and forget like MoveGold: the item events update when the
    // server answers. false for an item that is not on this tab, a count of 0,
    // more than one to the cursor, a combination the tab cannot do, or a closed
    // panel; true once the request is issued.
    bool Withdraw(const Unit& item, uint32_t count, StashWithdrawTarget target) const;
    // Advanced stash tabs only. Move `item` - on the cursor or carried - onto the
    // tab's stack of its class, the same as dropping it on the tab. The stash
    // panel must be open. Fire and forget; the server refuses a full stack (99).
    // false on any other tab, for an item of a class the tab does not stack, one
    // the player does not hold, or a closed panel; true once the request is issued.
    bool Deposit(const Unit& item) const;
    // Move gold between the carried gold and this tab; `mode` is GoldActionMode
    // Deposit or Withdraw. Fire and forget like GoldAction: Gold() and the gold
    // stats update when the server's reply lands. A shared pool may only support
    // moving "as much as fits" and ignore `amount`. false for a tab that holds no
    // gold, nothing to move, or no player unit; true once the request is issued.
    bool MoveGold(GoldActionMode mode, uint32_t amount) const;

   private:
    StashTabKind kind_;
    uint32_t index_;
};

// Every tab of the local player's stash, personal first, then shared. Empty when
// there is no player unit.
std::vector<StashTab> GetStashTabs();

}  // namespace d2bs::game
