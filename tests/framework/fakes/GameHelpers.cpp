#include "game/GameHelpers.h"

#include "fakes/GameLoopCollaborators.h"
#include "game/Level.h"
#include "game/Party.h"
#include "game/Unit.h"

namespace d2bs::game {

// === Game State ===
GameState GetGameState() {
    return d2bs::test::State().clientState;
}
bool IsGameReady() {
    return false;
}
bool IsInGame() {
    return false;
}
bool WaitForGameReady(std::chrono::milliseconds /*timeout*/) {
    return false;
}
uint32_t GetScreenSize() {
    return 0;
}
std::string GetWindowTitle() {
    return "";
}
Difficulty GetDifficulty() {
    return Difficulty::Normal;
}
uint32_t GetMapSeed() {
    return 0;
}
uint32_t GetPing() {
    return 0;
}
uint32_t GetFPS() {
    return 0;
}
bool GetAutomapOn() {
    return false;
}
void SetAutomapOn(bool /*value*/) {}
bool GetAlwaysRun() {
    return false;
}
void SetAlwaysRun(bool /*value*/) {}
bool GetNoPickUp() {
    return false;
}
void SetNoPickUp(bool /*value*/) {}
uint32_t GetWeaponSwitch() {
    return 0;
}
uint32_t GetGameType() {
    return 0;
}
uint32_t GetMercReviveCost() {
    return 0;
}
uint32_t GetLocale() {
    return 0;
}

// === BnetData Queries ===
std::string GetAccountName() {
    return "";
}
std::string GetPlayerName() {
    return "";
}
std::string GetRealmName() {
    return "";
}
std::string GetRealmShort() {
    return "";
}
Difficulty GetMaxDiff() {
    return Difficulty::Normal;
}
uint32_t GetCharFlags() {
    return 0;
}
std::optional<uint8_t> IsLadder() {
    return {};
}

// === GameStructInfo Queries ===
std::string GetGameName() {
    return "";
}
std::string GetGamePassword() {
    return "";
}
std::string GetGameServerIp() {
    return "";
}

// === Mouse/Screen ===
Position GetMousePos() {
    return Position::Zero;
}
uint32_t GetCursorType(bool /*isShop*/) {
    return 0;
}
Point ScreenToAutomap(Point p) {
    return p;
}
Point AutomapToScreen(Point p) {
    return p;
}
Point AbsScreenToMap(Point p) {
    return p;
}

// === UI ===
bool GetUIFlag(uint32_t /*flag*/) {
    return false;
}

// === Text Rendering ===
Size GetTextSize(const std::string& /*text*/, uint32_t /*font*/) {
    return Size::Zero;
}
void DrawGameText(const std::string& /*text*/, Point /*pos*/, uint32_t /*color*/, uint32_t /*font*/) {}

// === Drawing ===
void DrawRectangle(Point /*p1*/, Point /*p2*/, uint32_t /*color*/, uint32_t /*opacity*/) {}
void DrawLine(Point /*p1*/, Point /*p2*/, uint32_t /*color*/, uint32_t /*opacity*/) {}
void DrawFrame(Point /*p1*/, Point /*p2*/) {}

// === Network ===
void SendGamePacket(std::span<const uint8_t> /*data*/) {}
void ReceiveGamePacket(std::span<const uint8_t> /*data*/) {}

// === Chat ===
void PrintGameString(const std::string& /*text*/, int32_t /*color*/) {}
void Say(const std::string& /*text*/) {}

// === Trade ===
std::optional<std::string> GetTradeInfo(TradeInfoMode /*mode*/) {
    return std::nullopt;
}
bool IsTradeAccepted() {
    return false;
}
int32_t GetRecentTradeId() {
    return 0;
}
bool IsTradeBlocked() {
    return false;
}
bool AcceptTrade() {
    // Fake: unconditionally succeed.
    return true;
}
bool TradeOK() {
    return false;
}

// === Game Actions ===
void ExitGame() {
    ++d2bs::test::State().exitGameCount;
}
bool ClickMapAt(uint32_t /*clickType*/, uint32_t /*shift*/, Point /*pos*/) {
    return false;
}
bool ClickMapAt(uint32_t /*clickType*/, uint32_t /*shift*/, const Unit& /*unit*/) {
    return false;
}
bool SubmitItem(const Unit& /*item*/) {
    return false;
}
void Transmute() {}
bool TestPvpFlag(const Unit& /*a*/, const Unit& /*b*/, uint32_t /*flag*/) {
    return false;
}
bool HasWaypoint(uint32_t /*waypointId*/) {
    return false;
}
bool IsTownByLevelNo(uint32_t levelNo) {
    auto& s = d2bs::test::State();
    return s.isTown && levelNo == s.areaId;
}
std::string GetLocaleString(uint16_t /*localeId*/) {
    return "";
}
int32_t GetBaseStat(const std::string& /*table*/, uint32_t /*row*/, const std::string& /*column*/) {
    return 0;
}
int32_t GetQuestFlag(uint32_t /*act*/, uint32_t /*quest*/) {
    return 0;
}

// === Weapon / Stat / Skill Actions ===
void SwapWeapon() {}
void UseStatPoint(uint32_t /*stat*/, uint32_t /*count*/) {}
void UseSkillPoint(uint32_t /*skill*/, uint32_t /*count*/) {}
void TakeScreenshot() {}

// === Item Actions ===
ClickResult ClickBodyLocation(BodyLocation /*slot*/, InventoryOwner /*owner*/) {
    return ClickResult::InvalidTarget;
}
ClickResult ClickItem(ClickButton /*button*/, const Unit& /*item*/) {
    return ClickResult::InvalidTarget;
}
ClickResult ClickContainerSlot(ClickButton /*button*/, Position /*gridPos*/, ItemLocation /*container*/) {
    return ClickResult::InvalidTarget;
}
void ClickPartyMember(const Party& /*rosterUnit*/, PartyMode /*mode*/) {}
void LeaveParty() {}
uint32_t CheckUnitCollision(const game::Unit& /*unit1*/, const game::Unit& /*unit2*/, uint32_t /*mask*/) {
    return 0;
}

// === Misc ===
void PlayGameSound(uint32_t /*soundId*/) {}
void* GetHwnd() {
    return nullptr;
}
void GoldAction(GoldActionMode /*mode*/, int32_t /*amount*/) {}
void MoveNPC(uint32_t /*npcId*/, Position /*pos*/) {}
bool RevealLevel(uint32_t /*levelNo*/, bool /*drawPresets*/) {
    return false;
}

// === NPC Interaction ===
bool IsScrollingText() {
    return false;
}
void Cancel(CancelMode /*mode*/) {}

// === Dialog ===
std::vector<DialogLine> GetDialogLines() {
    return {};
}
bool SelectDialogLineByText(const std::string& /*text*/) {
    return false;
}

// === Input Simulation ===
void SendMouseClick(Point /*pos*/, int32_t /*type*/) {}
void SendKeyPress(uint32_t /*msg*/, uint32_t /*key*/, uint32_t /*extra*/) {}

// === Pathfinding ===
// Tests pre-populate `CollisionLookup::secondary` with all required level grids
// (see e.g. KurastBenchmarkTest cross-level test). The lazy-load fallback in
// `CollisionLookup::Get` that calls FindLevelAt is therefore never triggered;
// returning an empty Level here is sufficient for the test surface.
Level FindLevelAt(Position /*gamePos*/) {
    return Level();
}

// === MPQ ===
void LoadMpq(const std::string& /*path*/) {}

// === Launch ===
std::optional<std::string> GetLaunchProfile() {
    return std::nullopt;
}

}  // namespace d2bs::game
