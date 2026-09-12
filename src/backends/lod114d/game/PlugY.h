#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "game/Types.h"

namespace d2bs::game {
class Unit;
}  // namespace d2bs::game

// PlugY's multi-page stash (see docs/plugy_stash.md). Everything PlugY-specific in
// the backend lives here and in imports/extras/PlugY.h; Stash.cpp falls back to the
// vanilla single tab whenever HasPages() is false.
namespace d2bs::game::plugy {

// If PlugY.dll is already in the process (the manager injected it ahead of d2bs
// instead of launching through PlugY.exe), arranges for PlugY's exported Init to
// run at the point PlugY.exe would run it: Game.exe's startup LoadLibraryA call,
// after Fog's memory pool exists (Init allocates through it) and before anything
// PlugY's startup-time features hook has executed. Calling Init any earlier, e.g.
// straight from DllMain, crashes inside PlugY. No-op without the module, or when
// PlugY.exe already redirected that call itself.
void InstallInitHook();

// A supported PlugY build is loaded and has its multi-page stash extension
// installed in this process. Detected once, on first use.
bool IsActive();

// IsActive() and the local player's page mirror is populated. False out of game,
// and on Battle.net where PlugY leaves the stash untouched.
bool HasPages();

// The page lists as stash tabs: personal pages first, then shared.
std::vector<StashTab> GetStashTabs();

// Items on one page. Empty for an unknown page.
std::vector<Unit> GetStashTabItems(StashTabKind kind, uint32_t index);

// The page holding `item`, searched across both lists and the active page.
std::optional<StashTab> FindStashTab(const Unit& item);

// Makes `kind/index` the active page, runs `action`, then restores the page that
// was active before. Blocks the calling script thread through both switches
// (read locks are released while waiting, so the game thread keeps running).
// StashTabUnavailable when the page is unknown or a switch does not complete.
ClickResult WithActivePage(StashTabKind kind, uint32_t index, const std::function<ClickResult()>& action);

// ClickItem for an item parked on an inactive page: swaps its page in, clicks,
// swaps back. InvalidTarget if the item is not on an inactive page.
ClickResult ClickParkedItem(ClickButton button, const Unit& item);

// Move gold between the carried gold and PlugY's shared pool. PlugY's commands
// carry no amount: Deposit moves all carried gold the pool can take, Withdraw as
// much of the pool as the character can carry. Fire and forget, like the panel
// button; the mirror's sharedGold updates when the server's reply lands. false
// when there is no shared pool, the mode is not Deposit / Withdraw, or nothing
// can move.
bool MoveSharedGold(GoldActionMode mode);

}  // namespace d2bs::game::plugy
