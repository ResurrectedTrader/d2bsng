#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "game/Types.h"

namespace d2bs::game {

enum class UnitType : uint32_t;
class Level;
class Party;
class Unit;

// === Game State ===

GameState GetGameState();
bool IsGameReady();
bool IsInGame();
// A zero timeout (the default) falls back to WAIT_GAME_READY_DEFAULT for a
// transient Busy state; Menu/Null return false immediately, InGame true.
bool WaitForGameReady(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
// Actual viewport dimensions in pixels. OOG-safe: the menu always renders at
// 800x600 in 1.14d, where the in-game size variables are stale. The JS
// `me.screensize` resolution mode (0/1) is derived from this.
Size GetViewportSize();
std::string GetWindowTitle();
Difficulty GetDifficulty();
uint32_t GetMapSeed();
uint32_t GetPing();
uint32_t GetFPS();
bool GetAutomapOn();
void SetAutomapOn(bool value);
bool GetAlwaysRun();
void SetAlwaysRun(bool value);
bool GetNoPickUp();
void SetNoPickUp(bool value);
uint32_t GetWeaponSwitch();
uint32_t GetGameType();
uint32_t GetMercReviveCost();
uint32_t GetLocale();

// === BnetData Queries ===
std::string GetAccountName();
std::string GetPlayerName();
std::string GetRealmName();
std::string GetRealmShort();
Difficulty GetMaxDiff();
uint32_t GetCharFlags();
std::optional<uint8_t> IsLadder();

// === GameStructInfo Queries ===
std::string GetGameName();
std::string GetGamePassword();
std::string GetGameServerIp();

// === Mouse/Screen ===
Position GetMousePos();
uint32_t GetCursorType(bool isShop = false);
Point ScreenToAutomap(Point p);
Point AutomapToScreen(Point p);
Point AbsScreenToMap(Point p);

// === UI ===
bool GetUIFlag(uint32_t flag);

// === Text Rendering ===
Size GetTextSize(const std::string& text, uint32_t font);
void DrawGameText(const std::string& text, Point pos, uint32_t color, uint32_t font);

// === Drawing ===
void DrawRectangle(Point p1, Point p2, uint32_t color, uint32_t opacity);
void DrawLine(Point p1, Point p2, uint32_t color, uint32_t opacity);
void DrawFrame(Point p1, Point p2);

// === Network ===
void SendGamePacket(std::span<const uint8_t> data);
void ReceiveGamePacket(std::span<const uint8_t> data);

// === Chat ===
void PrintGameString(const std::string& text, int32_t color);
void Say(const std::string& text);

// === Trade ===
// Returns the queried trade info as a string, or nullopt if the source data is
// not available. Numeric modes (RecentTradeId / RecentTradeId2) render the id
// as decimal so the JS API can parseInt() it back.
std::optional<std::string> GetTradeInfo(TradeInfoMode mode);
bool IsTradeAccepted();
int32_t GetRecentTradeId();
bool IsTradeBlocked();
bool AcceptTrade();
bool TradeOK();

// === Game Actions ===
void ExitGame();
bool ClickMapAt(uint32_t clickType, bool shift, Point pos);
bool ClickMapAt(uint32_t clickType, bool shift, const Unit& unit);
bool SubmitItem(const Unit& item);
void Transmute();
bool TestPvpFlag(const Unit& a, const Unit& b, uint32_t flag);
bool HasWaypoint(uint32_t waypointId);
bool IsTownByLevelNo(uint32_t levelNo);
std::string GetLocaleString(uint16_t localeId);

// Result of a cell lookup in a game data .txt table (skills / monstats / itemstatcost / etc.).
// Maps 1:1 to JS return shapes:
//   monostate -> undefined (OOB row / unsupported column type / unknown table or column)
//   int64_t   -> Number    (numeric columns; int64_t losslessly holds unsigned 32-bit DWORD columns)
//   string    -> String    (ASCII / item-code / raw-code columns)
using TxtValue = std::variant<std::monostate, int64_t, std::string>;

TxtValue GetTxtValue(std::string_view table, uint32_t row, std::string_view column);

// Number of rows in the named .txt table, or nullopt if the table name is
// unknown or its game data is not currently loaded (e.g. out of game).
std::optional<uint32_t> GetTxtTableRowCount(std::string_view table);

int32_t GetQuestFlag(uint32_t quest, uint32_t flag);
// === Weapon / Stat / Skill Actions ===
void SwapWeapon();
void UseStatPoint(uint32_t stat, uint32_t count);
void UseSkillPoint(uint32_t skill, uint32_t count);
void TakeScreenshot();

// === Item Actions ===

// Cell dimensions of a grid container (Inventory/Trade/Cube/Stash), read from the
// layout the game itself draws the panel from. That layout is built from the
// compiled inventory.txt, which a mod may rewrite - PlugY's ActiveBigStash turns the
// 6x8 stash into 10x10 - so a hardcoded vanilla size puts items outside the grid.
// nullopt for slot containers, and while the layout is still unpopulated (the game
// fills it on first panel open; this force-runs that init).
std::optional<Size> GetGridSize(ItemLocation location);

// === Stash Tabs ===

// Tabs of the local player's stash, personal first, then shared. Vanilla LoD has
// exactly one active personal tab; paged and shared stashes add more. Empty when
// there is no player unit.
std::vector<StashTab> GetStashTabs();

// Items on one tab, active or not. The active tab's items are the stash items of
// the player's inventory. A backend that parks inactive tabs outside the
// inventory still keeps those units alive, so they resolve as regular handles.
// Empty for an unknown tab.
std::vector<Unit> GetStashTabItems(StashTabKind kind, uint32_t index);

// Left-click a grid cell of a stash tab, active or not: picks up the item there,
// drops the cursor item, or swaps, exactly as ClickContainerSlot(Left, cell, Stash)
// does on the active tab. The stash panel must be open. A backend whose packets
// cannot address an inactive tab blocks the calling script thread while the tab
// is swapped in, clicked, and the previous tab restored. StashTabUnavailable for
// an unknown tab or a switch that did not complete. ClickItem(button, item)
// likewise reaches items on inactive tabs.
ClickResult ClickStashTabSlot(StashTabKind kind, uint32_t index, Position gridPos);

// Move gold between the character's carried gold and a stash tab: Deposit takes
// from the carried gold, Withdraw puts back. The stash panel must be open. Only
// Deposit / Withdraw are meaningful modes. On the tab that carries the
// character's stash gold this is the vanilla gold dialog; a shared pool may only
// support moving "as much as fits" and ignore `amount`. false for an unknown
// tab, a tab that holds no gold, a mode that is not Deposit / Withdraw, or
// nothing to move; true once the request is issued. Fire and forget like
// GoldAction: the tab's gold in GetStashTabs and the gold stats update when the
// server's reply lands, so callers poll for the change as they do after gold().
bool StashTabGold(StashTabKind kind, uint32_t index, GoldActionMode mode, uint32_t amount);

// Toggle a body slot.
//   owner=Player    -> BodyClickTable[slot] invoked with (player, inv, slot); slot must be in [1..10].
//   owner=Mercenary -> MercItemAction(0x61, slot); slot must be in {Head(1), Body(3), RightPrimary(4)}.
// Out-of-range slot, missing click fn, or (owner=Mercenary) no merc -> InvalidTarget.
// Transaction dialog + cursor-hover reset are handled internally.
ClickResult ClickBodyLocation(BodyLocation slot, InventoryOwner owner);

// Click an item. Dispatches internally based on the item's current location:
//   Inventory/Stash/Cube         -> grid click via the container layout
//   Belt                         -> belt-slot click using item.dwPosX
//   Cursor item                  -> equip into body slot `button` (slot in [1..12])
//   Equipped                     -> toggle item's current body slot (button unused)
//   Merc-owned + button==Mercenary -> merc equip action
//   else (Ground, etc.)          -> InvalidTarget
// Non-UNIT_ITEM unit -> NotAnItem.
// Transaction dialog + cursor-hover reset are handled internally.
ClickResult ClickItem(ClickButton button, const Unit& item);

// Click at a grid cell in a specific container.
//   Inventory/Trade/Cube/Stash -> grid cell -> pixel via container layout;
//                                 auto-nudges for multi-cell cursor items.
//   Belt                       -> grid cell -> belt slot via the belt table.
// Unknown container or no matching belt cell -> InvalidTarget.
// Transaction dialog + cursor-hover reset are handled internally.
ClickResult ClickContainerSlot(ClickButton button, Position gridPos, ItemLocation container);
void ClickPartyMember(const Party& party, PartyMode mode);
void LeaveParty();
uint32_t CheckUnitCollision(const Unit& unit1, const Unit& unit2, uint32_t mask);
// === Skill Name Tables ===
std::optional<uint16_t> GetSkillByName(std::string_view name);

// === IPC ===
// Send an IPC message. If targetHwnd != 0, send to that target directly.
// Otherwise find the target by windowClassName/windowName (both empty = broadcast).
int32_t SendIPC(uint32_t mode, std::string_view data, uintptr_t targetHwnd = 0, std::string_view windowClassName = {},
                std::string_view windowName = {});

// === Misc ===
void PlayGameSound(uint32_t soundId);
void* GetHwnd();

void GoldAction(GoldActionMode mode, int32_t amount);
void MoveNPC(uint32_t npcId, Position pos);
bool RevealLevel(uint32_t levelNo, bool drawPresets = false);

// === NPC Interaction ===
bool IsScrollingText();
void Cancel(CancelMode mode);

// === Dialog ===
std::vector<DialogLine> GetDialogLines();

// Invoke the dialog line handler whose text matches `text` (UTF-8, exact
// match against the same conversion `GetDialogLines()` produces). Text
// matching rather than indexing makes the call robust to dialog churn -
// if a different dialog has opened since `text` was captured, the call
// fails safely instead of triggering an unrelated handler. First-match
// wins on duplicate texts.
//
// Runs on the game thread - implementations must marshal there because
// dialog handlers mutate game UI state. Returns true when the handler
// ran, false otherwise (no active dialog, no matching line, line not
// selectable, or no handler bound). Reference parallel: my_clickDialog
// at JSGame.cpp:213-227, which throws "That dialog is not currently
// clickable." on the false branch.
bool SelectDialogLineByText(const std::string& text);

// === Input Simulation ===

// Deliver a single mouse click at `pos`. The port owns the full sequence:
// down/up message pair, any port-specific timing.
void SendClick(Point pos);

// Deliver a single key press (down + up). The port owns the full sequence -
// timing, prompt-hiding, virtual-key/message translation.
void SendKey(uint32_t key);

// === Pathfinding ===
Level FindLevelAt(Position gamePos);

// === MPQ ===
void LoadMpq(const std::string& path);

// === Launch ===
// Resolve any game-port-specific launch configuration identifying the profile
// to activate at startup. 1.14d parses `-profile <name>` from GetCommandLineW()
// per reference CommandLine.cpp; other ports may use environment variables,
// registry, or IPC. Returns nullopt when no launch profile is requested.
std::optional<std::string> GetLaunchProfile();

// Version string of the game backend this build is compiled against, e.g.
// "1.14d" for the lod114d backend. Fixed at compile time - the injected glue
// DLL links exactly one backend - so it identifies "which build" for
// diagnostics and analytics.
std::string GetBackendVersion();

// === Analytics ===
// Analytics-related launch switches, surfaced to the frontend analytics
// component (which lives in the js frontend and can't see the backend's
// LaunchOptions). These carry only the command-line values; the frontend
// applies environment-variable fallbacks and derives the ingest endpoint. See
// docs/analytics.md.
struct AnalyticsLaunchOptions {
    bool disabled = false;  // -noanalytics
};
AnalyticsLaunchOptions GetAnalyticsLaunchOptions();

// Backend-agnostic feature tags active this session (e.g. "multiInstance",
// "proxy", "realm"), which the framework folds into the analytics feature list.
// Each backend reports whatever applies to it; the framework neither enumerates
// nor interprets the tags. A tag only ever names a feature - never the value
// behind it (no proxy address, realm host, window title, or CD key).
std::vector<std::string> GetActiveFeatures();

// === Realms ===
// A Battle.net realm/gateway the client can connect to: a display name and a
// server host (hostname or IP). D2 dials gateways on the fixed BNCS port 6112.
struct RealmInfo {
    std::string name;
    std::string host;
};

// Enumerate the realms the client can connect to: D2's own gateway list plus
// any added via the `-realm` launch option. Read-only; ordering is D2's
// gateways first, then the -realm additions.
std::vector<RealmInfo> GetRealms();

}  // namespace d2bs::game
