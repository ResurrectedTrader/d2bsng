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
uint32_t GetScreenSize();
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

int32_t GetQuestFlag(uint32_t quest, uint32_t flag);
// === Weapon / Stat / Skill Actions ===
void SwapWeapon();
void UseStatPoint(uint32_t stat, uint32_t count);
void UseSkillPoint(uint32_t skill, uint32_t count);
void TakeScreenshot();

// === Item Actions ===

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
uint32_t CheckUnitCollision(const game::Unit& unit1, const game::Unit& unit2, uint32_t mask);
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

}  // namespace d2bs::game
