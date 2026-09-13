#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/StashTab.h"
#include "game/Types.h"

struct D2UnitStrc;

// PlugY's multi-page stash (see docs/plugy_stash.md). Everything PlugY-specific in
// the backend lives here and in imports/extras/PlugY.h; Stash.cpp delegates here
// while IsActive() && HasStashTabs() holds and otherwise serves the vanilla single
// tab.
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
// installed in this process. Detected on first use and cached once final: a
// missing module or a not-yet-applied patch is retried until a player unit
// exists, by which time PlugY's startup-time Init has long run, so from then on
// the answer is one atomic load. Every other function here requires it to be
// true: they read PlugY's tail past the player data, which only exists then.
bool IsActive();

// The local player's page mirror is populated. False out of game, and on
// Battle.net where PlugY leaves the stash untouched. Takes its own read lock.
bool HasStashTabs();

// A stored-mode item outside any inventory: PlugY parked it on an inactive page
// (the game zeroes the node byte on removal, so its location reads Ground). No
// vanilla item is ever in that state. `item` must be a UNIT_ITEM. Checks IsActive()
// itself, being the one entry point reached from the generic item paths.
bool IsParkedItem(const D2UnitStrc* item);

// Page-level reads of the client mirror. Callers hold a read lock.
uint32_t PageCount(StashTabKind kind);
bool HasPage(StashTabKind kind, uint32_t index);
// User-given page name, UTF-8; empty when unnamed or for an unknown page.
std::string PageName(StashTabKind kind, uint32_t index);
// PlugY's single shared gold pool (0 without a shared stash).
uint32_t SharedGold();
// Items on one page. Empty for an unknown page.
std::vector<Unit> GetPageItems(StashTabKind kind, uint32_t index);
// The page holding `item`, searched across both lists and the active page.
std::optional<StashTab> FindPage(const Unit& item);

// Makes `kind/index` the active page, runs `action`, then restores the page that
// was active before. Blocks the calling script thread through both switches
// (read locks are released while waiting, so the game thread keeps running).
// StashTabUnavailable when the page is unknown, a switch does not complete, or
// the call re-enters an operation already running on this thread.
ClickResult WithActivePage(StashTabKind kind, uint32_t index, const std::function<ClickResult()>& action);

// ClickItem for an item parked on an inactive page: swaps its page in, clicks,
// swaps back. InvalidTarget if the item is on no page.
ClickResult ClickParkedItem(ClickButton button, const Unit& item);

// Move gold between the carried gold and PlugY's shared pool. `mode` is Deposit or
// Withdraw (validated by the caller). PlugY's commands carry no amount: Deposit
// moves all carried gold the pool can take, Withdraw as much of the pool as the
// character can carry. Fire and forget, like the panel button; the mirror's
// sharedGold updates when the server's reply lands. false when there is no shared
// pool or nothing can move.
bool MoveSharedGold(GoldActionMode mode);

}  // namespace d2bs::game::plugy
