#include "game/GameHelpers.h"

#include <Windows.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "DrlgHelpers.h"
#include "RoomData.h"
#include "asm_thunks/asm_thunks.h"
#include "game/Bridge.h"
#include "game/Constants.h"
#include "game/Control.h"
#include "game/Finders.h"
#include "game/GameThread.h"
#include "game/LaunchOptions.h"
#include "game/Level.h"
#include "game/Party.h"
#include "game/Unit.h"
#include "hooks/HookManager.h"
#include "hooks/Intercepts.h"
#include "imports/BnClient.h"
#include "imports/D2Client.h"
#include "imports/D2Common.h"
#include "imports/D2Gfx.h"
#include "imports/D2Lang.h"
#include "imports/D2Launch.h"
#include "imports/D2Multi.h"
#include "imports/D2Net.h"
#include "imports/D2Win.h"
#include "imports/Storm.h"
#include "imports/extras/BnetData.h"
#include "imports/extras/GameStructInfo.h"
#include "imports/extras/MPQStats.h"
#include "imports/extras/TransactionDialogs.h"
#include "imports/extras/WindowHandlers.h"
#include "utils/utils.h"

#include "imports/extras/D2ActiveRoomStrc.h"
#include "imports/extras/D2DrlgActStrc.h"
#include "imports/extras/D2DrlgLevelStrc.h"
#include "imports/extras/D2DrlgRoomStrc.h"
#include "imports/extras/D2DrlgStrc.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2BitManip.h>            // D2BitBufferStrc
#include <DataTbls/ItemsTbls.h>    // D2ItemsTxt
#include <DataTbls/LevelsTbls.h>   // D2LevelsTxt
#include <DataTbls/ObjectsTbls.h>  // D2ObjectsTxt
#include <Path/Path.h>             // D2DynamicPathStrc
#include <Units/Units.h>           // D2UnitStrc
#pragma clang diagnostic pop

namespace d2bs::game {

using namespace d2bs::imports;
using d2bs::imports::extras::D2ActiveRoomStrc;
using d2bs::imports::extras::D2DrlgActStrc;
using d2bs::imports::extras::D2DrlgLevelStrc;

namespace {

constexpr std::chrono::milliseconds WAIT_GAME_READY_DEFAULT{15000};
// Polling cadence -- matches reference (Sleep(10)) closely enough for parity.
constexpr std::chrono::milliseconds WAIT_GAME_READY_POLL{10};

// Convert a fixed-size NUL-padded char buffer to std::string. The buffer is
// not guaranteed to be NUL-terminated when full, hence the explicit upper bound
// passed to strnlen.
template <size_t N>
std::string ToString(const std::array<char, N>& buf) {
    return std::string(buf.data(), strnlen(buf.data(), N));
}

// Container layout structs. The game zero-initialises these and lazily
// populates the cell-size bytes the first time the user opens the relevant
// panel via the in-game click dispatcher; until then, layout->nGridBoxWidth
// is 0 and any LeftClickItem_I call divides by zero. We hide the GameVar
// instances behind ResolveContainerLayout so callers can't bypass the
// init-on-first-touch dance. Self-registration happens at static-init.
//
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - GameVar
// instances must have static storage so they self-register with the import
// registry before Bridge::Init runs.
imports::GameVar<D2InventoryGridInfoStrc> tradeLayout{0x3BCA30};
imports::GameVar<D2InventoryGridInfoStrc> stashLayout{0x3BCA78};
imports::GameVar<D2InventoryGridInfoStrc> storeLayout{0x3BCB58};
imports::GameVar<D2InventoryGridInfoStrc> cubeLayout{0x3BCB70};
imports::GameVar<D2InventoryGridInfoStrc> inventoryLayout{0x3BCB88};
imports::GameVar<D2InventoryGridInfoStrc> mercLayout{0x3BCD4C};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

struct LayoutEntry {
    D2InventoryGridInfoStrc* layout;
    // The game's internal location enum passed as the `location` argument to
    // LeftClickItem_I / ClickItemRight_I. Different from ItemLocation: the
    // game-internal values are 0=Inventory, 2=Trade, 3=Cube, 4=Stash.
    uint32_t locationCode;
};

// Maps an ItemLocation to the matching layout pointer + the location code
// the click thunks expect. Returns {nullptr, 0} for locations that don't
// have a grid layout (Equipped, Belt, Body slots, etc).
LayoutEntry LookupContainerLayout(ItemLocation location) {
    switch (location) {
        case ItemLocation::Inventory:
            return {.layout = inventoryLayout.Ptr(), .locationCode = 0};
        case ItemLocation::Trade:
            return {.layout = tradeLayout.Ptr(), .locationCode = 2};
        case ItemLocation::Cube:
            return {.layout = cubeLayout.Ptr(), .locationCode = 3};
        case ItemLocation::Stash:
            return {.layout = stashLayout.Ptr(), .locationCode = 4};
        default:
            return {.layout = nullptr, .locationCode = 0};
    }
}

// Returns a layout pointer for the requested container, lazy-initialising it
// via D2_CLIENT_InitInventory if the user hasn't opened the panel yet this
// session. Must be called on the game thread. Returns nullopt if the layout
// still isn't usable after the init attempt -- callers should bail rather than
// feed it to a click thunk (LeftClickItem_I divides by the nGridBox bytes and
// crashes on zero).
std::optional<LayoutEntry> ResolveContainerLayout(ItemLocation location) {
    auto entry = LookupContainerLayout(location);
    if (entry.layout == nullptr) {
        return std::nullopt;
    }
    if (entry.layout->nGridBoxWidth == 0) {
        d2client::INVENTORY_Init();
    }
    if (entry.layout->nGridBoxWidth == 0 || entry.layout->nGridBoxHeight == 0) {
        spdlog::warn("[ResolveContainerLayout] location={} still uninitialised after InitInventory (w={} h={})",
                     static_cast<int32_t>(location), entry.layout->nGridBoxWidth, entry.layout->nGridBoxHeight);
        return std::nullopt;
    }
    return entry;
}

}  // namespace

// === Game State ===

// Reference: D2Helpers.cpp:125-147 ClientState - branches on player, control list, and full path/room/level chain.
GameState GetGameState() {
    auto* player = d2client::UNITS_GetPlayerUnit();
    auto* firstControl = *d2win::gpFirstControl;

    if (player == nullptr) {
        return firstControl != nullptr ? GameState::Menu : GameState::Null;
    }
    if (firstControl != nullptr) {
        // Both present -- Diablo II is mid-load (e.g., a control overlay on top
        // of an in-game scene). The reference treats this as Null.
        return GameState::Null;
    }
    if (player->pUpdateUnit != nullptr) {
        return GameState::Busy;
    }
    // `D2DynamicPathStrc::pRoom` is typed as `::D2ActiveRoomStrc*` (D2MOO); the
    // bytes follow the 1.14d allocation modeled by `extras::D2ActiveRoomStrc`,
    // hence the reinterpret_cast at the unit->path boundary.
    auto* pathRoom =
        player->pDynamicPath != nullptr ? reinterpret_cast<D2ActiveRoomStrc*>(player->pDynamicPath->pRoom) : nullptr;
    if (player->pInventory == nullptr || pathRoom == nullptr || pathRoom->pDrlgRoom == nullptr ||
        pathRoom->pDrlgRoom->pLevel == nullptr || pathRoom->pDrlgRoom->pLevel->nLevelId == 0) {
        return GameState::Busy;
    }
    return GameState::InGame;
}

bool IsGameReady() {
    return GetGameState() == GameState::InGame;
}

bool IsInGame() {
    return GetGameState() != GameState::Menu;
}

bool WaitForGameReady(std::chrono::milliseconds timeout) {
    const auto effective = (timeout.count() > 0) ? timeout : WAIT_GAME_READY_DEFAULT;
    const auto deadline = std::chrono::steady_clock::now() + effective;
    while (true) {
        switch (GetGameState()) {
            case GameState::InGame:
                return true;
            case GameState::Null:
            case GameState::Menu:
                return false;
            case GameState::Busy:
                break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(WAIT_GAME_READY_POLL);
    }
}

// Reference: D2Helpers.cpp:560-568 hard-codes 800x600 in the menu state
// because the in-game ScreenSize variables are stale OOG. The 800x600 fallback
// matches reference's GetScreenSize.
uint32_t GetScreenSize() {
    if (GetGameState() == GameState::Menu) {
        // The framework expects a single uint32 -- packed encoding mirrors
        // d2gfx::D2GFX_GetResolutionMode, which the reference uses to drive its GetScreenSize
        // small-vs-large decision. 0=640x480, 1=800x600 in 1.14d.
        return 1;
    }
    return d2gfx::D2GFX_GetResolutionMode();
}

std::string GetWindowTitle() {
    auto hwnd = d2gfx::WINDOW_GetWindow();
    if (hwnd == nullptr) {
        return {};
    }
    std::array<wchar_t, 256> title{};
    const auto len = ::GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    if (len <= 0) {
        return {};
    }
    return utils::ToStr(std::wstring(title.data(), static_cast<size_t>(len)), CP_UTF8);
}

Difficulty GetDifficulty() {
    return static_cast<Difficulty>(d2client::GAME_GetDifficulty());
}

uint32_t GetMapSeed() {
    return *d2client::gnMapId;
}

uint32_t GetPing() {
    return *d2client::gnPing;
}

uint32_t GetFPS() {
    return *d2client::gnFPS;
}

bool GetAutomapOn() {
    return *d2client::gbAutomapOn != 0;
}

void SetAutomapOn(bool value) {
    *d2client::gbAutomapOn = value ? 1U : 0U;
}

bool GetAlwaysRun() {
    return *d2client::gbAlwaysRun != 0;
}

void SetAlwaysRun(bool value) {
    *d2client::gbAlwaysRun = value ? 1U : 0U;
}

bool GetNoPickUp() {
    return *d2client::gbNoPickUp != 0;
}

void SetNoPickUp(bool value) {
    *d2client::gbNoPickUp = value ? 1U : 0U;
}

uint32_t GetWeaponSwitch() {
    return *d2client::gnWeaponSwitch;
}

uint32_t GetGameType() {
    return *d2client::gbExpCharFlag;
}

uint32_t GetMercReviveCost() {
    return *d2client::gnMercReviveCost;
}

uint32_t GetLocale() {
    return *d2client::gnLang;
}

// === BnetData Queries ===

std::string GetAccountName() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? ToString(data->szAccountName) : std::string{};
}

std::string GetPlayerName() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? ToString(data->szPlayerName) : std::string{};
}

std::string GetRealmName() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? ToString(data->szRealmName) : std::string{};
}

std::string GetRealmShort() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? ToString(data->szRealmName2) : std::string{};
}

Difficulty GetMaxDiff() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? static_cast<Difficulty>(data->nMaxDiff) : Difficulty::Normal;
}

uint32_t GetCharFlags() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? static_cast<uint32_t>(data->nCharFlags) : 0U;
}

std::optional<uint8_t> IsLadder() {
    auto* data = *d2launch::gpBnetData;
    return data != nullptr ? std::optional<uint8_t>{data->nLadderFlag} : std::nullopt;
}

// === GameStructInfo Queries ===

std::string GetGameName() {
    auto* info = *d2client::gpGameInfo;
    return info != nullptr ? ToString(info->szGameName) : std::string{};
}

std::string GetGamePassword() {
    auto* info = *d2client::gpGameInfo;
    return info != nullptr ? ToString(info->szGamePassword) : std::string{};
}

std::string GetGameServerIp() {
    auto* info = *d2client::gpGameInfo;
    return info != nullptr ? ToString(info->szGameServerIp) : std::string{};
}

// === Mouse/Screen ===

Position GetMousePos() {
    return {.x = *d2client::gnMouseX, .y = *d2client::gnMouseY};
}

uint32_t GetCursorType(bool isShop) {
    return isShop ? *d2client::gnShopCursorType : *d2client::gnRegularCursorType;
}

// Reference: D2Helpers.cpp:364-376 ScreenToAutomap. Maps a screen pixel into
// automap-cell coords through the offset/divisor variables. The +5 / -1
// adjustment when GetAutomapSize() is non-zero matches the large-resolution
// path. Reference takes screen pixels in; we receive a Point and scale by 32
// internally -- same arithmetic, more obvious shape.
Point ScreenToAutomap(Point p) {
    const int32_t scaledX = p.x * 32;
    const int32_t scaledY = p.y * 32;
    const int32_t divisor = std::max<int32_t>(*d2client::gnAutomapMode, 1);
    auto offset = *d2client::gAutomapOffset;
    Point out{
        .x = ((scaledX - scaledY) / 2 / divisor) - offset.x + 8,
        .y = ((scaledX + scaledY) / 4 / divisor) - offset.y - 8,
    };
    if (d2client::AUTOMAP_GetSize() != 0) {
        out.x -= 1;
        out.y += 5;
    }
    return out;
}

// Reference: D2Helpers.cpp:378-381 AutomapToScreen. Inverse of ScreenToAutomap
// for cell->pixel. Mode multiplier scales tiles to current automap zoom.
Point AutomapToScreen(Point p) {
    auto offset = *d2client::gAutomapOffset;
    const int32_t mode = *d2client::gnAutomapMode;
    return {
        .x = 8 - offset.x + (p.x * mode),
        .y = 8 + offset.y + (p.y * mode),
    };
}

// Reference: JSGame.cpp:1308-1312 -- applies the viewport offset then runs the
// game's screen->map conversion. Mirrors `getMouseCoords(true)` semantics.
Point AbsScreenToMap(Point p) {
    p.x += *d2client::gnMouseOffsetX;
    p.y += *d2client::gnMouseOffsetY;
    d2common::AUTOMAP_AbsScreenToMap(&p.x, &p.y);
    return p;
}

// === UI ===

bool GetUIFlag(uint32_t flag) {
    return d2client::UI_GetVar(flag) != 0;
}

// === Text Rendering ===

Size GetTextSize(const std::string& text, uint32_t font) {
    auto wide = utils::ToWStr(text, CP_UTF8);
    const auto oldSize = d2win::D2WIN_SetTextSize(font);
    uint32_t width = 0;
    uint32_t fileNo = 0;
    const auto height = d2win::D2WIN_GetTextSize(wide.c_str(), &width, &fileNo);
    d2win::D2WIN_SetTextSize(oldSize);
    return {.width = width, .height = height};
}

void DrawGameText(const std::string& text, Point pos, uint32_t color, uint32_t font) {
    auto wide = utils::ToWStr(text, CP_UTF8);
    const auto oldSize = d2win::D2WIN_SetTextSize(font);
    d2win::D2WIN_DrawText(wide.c_str(), pos.x, pos.y, color, 0);
    d2win::D2WIN_SetTextSize(oldSize);
}

// === Drawing ===

void DrawRectangle(Point p1, Point p2, uint32_t color, uint32_t opacity) {
    d2gfx::D2GFX_DrawRectangle(p1.x, p1.y, p2.x, p2.y, color, opacity);
}

void DrawLine(Point p1, Point p2, uint32_t color, uint32_t opacity) {
    d2gfx::D2GFX_DrawLine(p1.x, p1.y, p2.x, p2.y, color, opacity);
}

void DrawFrame(Point p1, Point p2) {
    if (GetGameState() != GameState::InGame) {
        return;
    }
    // The DrawRectFrame import takes the address of a contiguous int32 quad
    // (left, top, right, bottom) -- match that layout via a stack buffer.
    std::array<int32_t, 4> rect = {p1.x, p1.y, p2.x, p2.y};
    d2client::UI_DrawRectFrame(rect.data());
}

// === Network ===

void SendGamePacket(std::span<const uint8_t> data) {
    if (data.empty()) {
        return;
    }
    d2net::CLIENT_Send(data.size(), 1, data.data());
}

void ReceiveGamePacket(std::span<const uint8_t> data) {
    if (data.empty()) {
        return;
    }
    d2net::CLIENT_DequeueGamePacket(data.data(), static_cast<uint32_t>(data.size()));
}

// === Chat ===

void PrintGameString(const std::string& text, int32_t color) {
    if (text.empty()) {
        return;
    }
    auto wide = utils::ToWStr(text, CP_UTF8);
    d2client::UI_PrintGameString(wide.c_str(), color);
}

void Say(const std::string& text) {
    if (text.empty()) {
        return;
    }
    // Reference Core.cpp:85-132. Copy message into ChatMsg (wchar_t* in-game,
    // char* OOG), build a WM_CHAR+VK_RETURN MSG, then enter the case-13 body
    // of the chat-input handler via the Say_ASM naked thunk.
    // Reference passes &aMsg (MSG**) -- likely a vestigial bug; we pass MSG*
    // directly so *(a1+8) lands on MSG::wParam as IDA's decompile expects.
    if (d2client::UNITS_GetPlayerUnit() == nullptr) {
        // Reference Core.cpp:124-128 OOG path: BNet channel chat dispatched
        // through D2MULTI_DoChat. Gated on the help button being present
        // (signals we're on the channel screen) and the disconnect-OK button
        // being absent (signals no modal blocking). The buffer is ASCII --
        // ChatBoxMsg is char* whereas the in-game ChatMsg is wchar_t*.
        auto helpBtn = Control::Find(ControlType::Button, 187U, 470U, 80U, 20U);
        auto disconnect = Control::Find(ControlType::Button, 351U, 337U, 96U, 32U);
        if (!helpBtn || disconnect) {
            return;
        }
        auto wide = utils::ToWStr(text);
        auto ansi = utils::ToStr(wide, CP_ACP);
        auto* dst = imports::d2multi::gszChatBoxMsg.Ptr()->data();
        if (dst == nullptr) {
            return;
        }
        std::memcpy(dst, ansi.c_str(), ansi.size() + 1);
        GameThread::Execute([] { d2multi::D2MULTI_DoChat(); });
        return;
    }
    auto wide = utils::ToWStr(text);
    auto* chatBuf = imports::d2client::gwszChatMsg.Ptr()->data();
    if (chatBuf == nullptr) {
        return;
    }
    std::memcpy(chatBuf, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));

    MSG msg{};
    msg.hwnd = imports::d2gfx::WINDOW_GetWindow();
    msg.message = WM_CHAR;
    msg.wParam = VK_RETURN;
    msg.lParam = 0x11C0001;
    msg.pt.x = 0x79;
    msg.pt.y = 0x1;
    asm_thunks::Say(&msg);
}

// === Trade ===

std::optional<std::string> GetTradeInfo(TradeInfoMode mode) {
    // Reference JSGame.cpp:115-122 returns the trade id as a number for both
    // RecentTradeId modes and the player name for RecentTradeName. The
    // framework receives a string for all three -- render the id as decimal so
    // the JS API can parseInt() it back when needed.
    switch (mode) {
        case TradeInfoMode::RecentTradeId:
        case TradeInfoMode::RecentTradeId2:
            return std::to_string(*d2client::gnRecentTradeId);
        case TradeInfoMode::RecentTradeName: {
            auto* data = *d2launch::gpBnetData;
            if (data == nullptr) {
                return std::nullopt;
            }
            auto name = ToString(data->szPlayerName);
            if (name.empty()) {
                return std::nullopt;
            }
            return name;
        }
    }
    return std::nullopt;
}

bool IsTradeAccepted() {
    return *d2client::gbTradeAccepted != 0;
}

int32_t GetRecentTradeId() {
    return *d2client::gnRecentTradeId;
}

bool IsTradeBlocked() {
    return *d2client::gbTradeBlock != 0;
}

// Reference JSGame.cpp:106-145 toggles via *p_D2CLIENT_bTradeAccepted between
// AcceptTrade and CancelTrade, gated by trade state == 3/5/7 and !TradeBlock.
// We mirror the same gate, plus dispatch to the game thread because the
// underlying game functions mutate UI state.
bool AcceptTrade() {
    if (!WaitForGameReady()) {
        return false;
    }
    auto guard = Bridge::Lock();
    const auto state = *d2client::gnRecentTradeId;
    if (state != 3 && state != 5 && state != 7) {
        return false;
    }
    if (*d2client::gbTradeBlock != 0) {
        return false;
    }
    return GameThread::Execute([]() -> bool {
        if (*d2client::gbTradeAccepted != 0) {
            *d2client::gbTradeAccepted = 0;
            d2client::TRADE_Cancel();
        } else {
            *d2client::gbTradeAccepted = 1;
            d2client::TRADE_Accept();
        }
        return true;
    });
}

// Reference JSGame.cpp:147-168 walks the dialog list and confirms the
// per-line `handler` field matches the resolved address of D2CLIENT_TradeOK
// before invoking the function (and requires *p_TransactionDialogs == 1 to
// avoid the documented crash if the line is selected without that gate).
bool TradeOK() {
    if (!WaitForGameReady()) {
        return false;
    }
    auto guard = Bridge::Lock();
    auto* tdi = *d2client::gpTransactionDialogsInfo;
    if (tdi == nullptr || *d2client::gnTransactionDialogs != 1U) {
        return false;
    }
    auto* tradeOk = d2client::TRADE_OK.Ptr();
    if (tradeOk == nullptr) {
        return false;
    }
    bool matched = false;
    const auto count = std::min<uint32_t>(tdi->dwNumLines, static_cast<uint32_t>(tdi->aDialogLines.size()));
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by count above
    for (uint32_t i = 0; i < count && !matched; ++i) {
        if (tdi->aDialogLines[i].pfHandler == tradeOk) {
            matched = true;
        }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    if (!matched) {
        return false;
    }
    return GameThread::Execute([]() -> bool {
        d2client::TRADE_OK();
        return true;
    });
}

// === Game Actions ===

void ExitGame() {
    d2client::GAME_Exit();
}

// Reference Core.cpp:134-179 ClickMap. Captures previous mouse coordinates,
// translates the world click through MapToAbsScreen, applies viewport offsets,
// dispatches via D2CLIENT_ClickMap, and restores mouse coords. The shift /
// always-run handling is folded into the click flag.
bool ClickMapAt(uint32_t clickType, bool shift, Point pos) {
    if (GetGameState() != GameState::InGame) {
        return false;
    }
    return GameThread::Execute([clickType, shift, pos]() -> bool {
        Point click = pos;
        d2common::AUTOMAP_MapToAbsScreen(&click.x, &click.y);
        click.x -= *d2client::gnMouseOffsetX;
        click.y -= *d2client::gnMouseOffsetY;
        const auto savedX = *d2client::gnMouseX;
        const auto savedY = *d2client::gnMouseY;
        *d2client::gnMouseX = 0;
        *d2client::gnMouseY = 0;
        const uint32_t runFlag = (*d2client::gbAlwaysRun != 0) ? 0x08U : 0U;
        const uint32_t flag = shift ? 0x0CU : runFlag;
        // Force-NULL through P5 so the click is unambiguously coord-only --
        // matches reference Core.cpp:168-173 (bClickAction=TRUE, unit=NULL).
        hooks::intercepts::WithSelectedUnit(nullptr, [&] {
            d2client::UI_ClickMap(clickType, static_cast<uint32_t>(click.x), static_cast<uint32_t>(click.y), flag);
        });
        *d2client::gnMouseX = savedX;
        *d2client::gnMouseY = savedY;
        return true;
    });
}

// Re-resolve via the public Unit::Find path so we don't need friend access to
// Unit::ResolvePtr. The lookup happens on the game thread under a fresh read
// lock -- the cached identity is what we trust, not the framework's own
// per-call HandleCache (which the framework owns).
bool ClickMapAt(uint32_t clickType, bool shift, const Unit& unit) {
    if (GetGameState() != GameState::InGame) {
        return false;
    }
    const auto unitId = unit.Id();
    const auto unitType = unit.Type();
    if (unitId == 0) {
        return false;
    }
    return GameThread::Execute([clickType, shift, unitId, unitType]() -> bool {
        auto* pUnit = d2client::UNITS_GetServerSideUnit(unitId, unitType);
        if (pUnit == nullptr) {
            pUnit = d2client::UNITS_GetClientSideUnit(unitId, unitType);
        }
        if (pUnit == nullptr) {
            return false;
        }
        Point click{.x = static_cast<int32_t>(d2common::UNITS_GetClientCoordX(pUnit)),
                    .y = static_cast<int32_t>(d2common::UNITS_GetClientCoordY(pUnit))};
        d2common::AUTOMAP_MapToAbsScreen(&click.x, &click.y);
        click.x -= *d2client::gnMouseOffsetX;
        click.y -= *d2client::gnMouseOffsetY;
        const auto savedX = *d2client::gnMouseX;
        const auto savedY = *d2client::gnMouseY;
        *d2client::gnMouseX = 0;
        *d2client::gnMouseY = 0;
        // Stage the unit for P5 -- unless it's the player, in which case
        // reference treats it as a coord-only click (Core.cpp:155).
        const bool unitIsPlayer = (pUnit == d2client::UNITS_GetPlayerUnit());
        const uint32_t runFlag = (*d2client::gbAlwaysRun != 0) ? 0x08U : 0U;
        const uint32_t flag = shift ? 0x0CU : runFlag;
        hooks::intercepts::WithSelectedUnit(unitIsPlayer ? nullptr : pUnit, [&] {
            d2client::UI_ClickMap(clickType, static_cast<uint32_t>(click.x), static_cast<uint32_t>(click.y), flag);
            // After a unit-targeted click, the game writes the target into its
            // SelectedUnit slot. Wipe it so subsequent ticks don't auto-continue
            // interacting with that target. Reference Core.cpp:162.
            if (!unitIsPlayer) {
                asm_thunks::SetSelectedUnit(nullptr);
            }
        });
        *d2client::gnMouseX = savedX;
        *d2client::gnMouseY = savedY;
        return true;
    });
}

// Reference JSGame.cpp:1344-1376. Two paths:
//   - Act 2, in the staff-tomb level: send packet 0x44 with the cube/orifice
//     mechanic and item ids. Used to commit the Horadric Staff.
//   - Acts 1 or 5 (nAct 0 or 4) with a recent interact id, in town: dispatch
//     via the SubmitItem import. This is the regular cube/quest-NPC
//     submission path.
bool SubmitItem(const Unit& item) {
    if (!WaitForGameReady()) {
        return false;
    }
    const auto itemId = item.Id();
    if (itemId == 0) {
        return false;
    }
    return GameThread::Execute([itemId]() -> bool {
        auto* cursor = d2common::INVENTORY_GetCursorItem();
        if (cursor == nullptr) {
            return false;
        }
        auto* player = d2client::UNITS_GetPlayerUnit();
        if (player == nullptr) {
            return false;
        }
        // `D2UnitStrc::pDrlgAct` is typed as D2MOO's `::D2DrlgActStrc*`; the bytes follow
        // the 1.14d allocation modeled by `extras::D2DrlgActStrc`. Same for the dynamic
        // path's `pRoom` (modeled by `extras::D2ActiveRoomStrc`).
        auto* act = reinterpret_cast<D2DrlgActStrc*>(player->pDrlgAct);
        const auto* drlg = (act != nullptr) ? act->pDrlg : nullptr;
        auto* pathRoom = player->pDynamicPath != nullptr
                             ? reinterpret_cast<D2ActiveRoomStrc*>(player->pDynamicPath->pRoom)
                             : nullptr;
        const auto* level =
            (pathRoom != nullptr && pathRoom->pDrlgRoom != nullptr) ? pathRoom->pDrlgRoom->pLevel : nullptr;
        const auto playerArea = (level != nullptr) ? static_cast<uint32_t>(level->nLevelId) : 0U;

        if (player->nAct == 1 && drlg != nullptr && playerArea == static_cast<uint32_t>(drlg->nStaffTombLevel)) {
            // Client->server packet 0x44 (Horadric staff commit). D2MOO does not
            // define a typed struct for this packet -- the only packet typings it
            // ships are framing types in `D2Net/include/Packet.h`. Build it as a
            // packed byte buffer; the layout matches reference JSGame.cpp:1357.
            *d2client::gnCursorItemMode = 3;
            std::array<uint8_t, 17> packet{};
            packet[0] = 0x44;
            std::memcpy(packet.data() + 1, &player->dwUnitId, sizeof(uint32_t));
            const uint32_t orifice = *d2client::gnOrificeId;
            std::memcpy(packet.data() + 5, &orifice, sizeof(uint32_t));
            // Reference reads the cursor item's id (JSGame.cpp:1357). All in-tree
            // callers pass the cursor unit, but read it through the resolved
            // pointer so a non-cursor handle never fabricates a mismatched packet.
            std::memcpy(packet.data() + 9, &cursor->dwUnitId, sizeof(uint32_t));
            const uint32_t mode = 3;
            std::memcpy(packet.data() + 13, &mode, sizeof(uint32_t));
            d2net::CLIENT_Send(packet.size(), 1, packet.data());
            return true;
        }
        if ((player->nAct == 0 || player->nAct == 4) && *d2client::gnRecentInteractId != 0 &&
            d2common::DUNGEON_IsTownLevelId(playerArea)) {
            d2client::ITEMS_Submit(itemId);
            return true;
        }
        return false;
    });
}

void Transmute() {
    if (!WaitForGameReady()) {
        return;
    }
    GameThread::Execute([]() {
        // Reference JSGame.cpp:1258-1273 toggles the cube UI on if it isn't
        // already open, runs Transmute, then restores the prior UI state.
        constexpr uint32_t UI_CUBE = 0x1A;
        const bool wasOpen = d2client::UI_GetVar(UI_CUBE) != 0;
        if (!wasOpen) {
            d2client::UI_SetVar(UI_CUBE, 1, 0);
        }
        d2client::ITEMS_Transmute();
        if (!wasOpen) {
            d2client::UI_SetVar(UI_CUBE, 0, 0);
        }
    });
}

bool TestPvpFlag(const Unit& a, const Unit& b, uint32_t flag) {
    return d2client::PLAYERLIST_CheckFlag(a.Id(), b.Id(), flag) != 0;
}

bool HasWaypoint(uint32_t waypointId) {
    constexpr uint32_t MAX_WAYPOINT_ID = 40;
    if (waypointId > MAX_WAYPOINT_ID) {
        return false;
    }
    return d2common::WAYPOINTS_IsActivated(*d2client::gpWaypointTable, static_cast<uint16_t>(waypointId)) != 0;
}

bool IsTownByLevelNo(uint32_t levelNo) {
    return d2common::DUNGEON_IsTownLevelId(levelNo);
}

std::string GetLocaleString(uint16_t localeId) {
    auto* wide = d2lang::D2LANG_GetLocaleText(localeId);
    if (wide == nullptr) {
        return {};
    }
    return utils::ToStr(std::wstring(wide), CP_UTF8);
}

TxtValue GetTxtValue(std::string_view table, uint32_t row, std::string_view column) {
    if (table.empty() || column.empty()) {
        return std::monostate{};
    }
    auto raw = imports::extras::GetTxtValue(table, row, column);
    if (auto* n = std::get_if<int64_t>(&raw)) {
        return *n;
    }
    if (auto* s = std::get_if<std::string>(&raw)) {
        return std::move(*s);
    }
    return std::monostate{};
}

int32_t GetQuestFlag(uint32_t quest, uint32_t flag) {
    return d2common::QUESTRECORD_GetQuestFlag(d2client::QUESTRECORD_GetQuestInfo(), quest, flag);
}

// === Weapon / Stat / Skill Actions ===

// Reference JSGame.cpp:1239-1251 -- packet 0x60. Gated by EXPAC because LoD
// classes are the only ones with a secondary weapon set.
void SwapWeapon() {
    auto* data = *d2launch::gpBnetData;
    if (data == nullptr || (data->nCharFlags & CHAR_FLAG_EXPAC) == 0) {
        return;
    }
    std::array<uint8_t, 1> packet = {0x60};
    d2net::CLIENT_Send(packet.size(), 1, packet.data());
}

// Reference Game.cpp:14-26 UseStatPoint -- packet 0x3A with the stat id, sent
// `count` times spaced by 500ms. We mirror the same cadence; the timing keeps
// the server's accept-state machine happy.
void UseStatPoint(uint32_t stat, uint32_t count) {
    if (count == 0) {
        return;
    }
    constexpr uint32_t STAT_STATPOINTSLEFT = 4;
    auto myUnit = Unit::Player();
    if (!myUnit) {
        return;
    }
    if (myUnit.GetStat(STAT_STATPOINTSLEFT, 0) < static_cast<int32_t>(count)) {
        return;
    }
    std::array<uint8_t, 3> packet{};
    packet[0] = 0x3A;
    const auto stat16 = static_cast<uint16_t>(stat);
    std::memcpy(packet.data() + 1, &stat16, sizeof(uint16_t));
    for (uint32_t i = 0; i < count; ++i) {
        d2net::CLIENT_Send(packet.size(), 1, packet.data());
        if (i + 1 != count) {
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }
}

// Reference Game.cpp:28-40 UseSkillPoint -- packet 0x3B, otherwise identical
// shape to UseStatPoint.
void UseSkillPoint(uint32_t skill, uint32_t count) {
    if (count == 0) {
        return;
    }
    constexpr uint32_t STAT_SKILLPOINTSLEFT = 5;
    auto myUnit = Unit::Player();
    if (!myUnit) {
        return;
    }
    if (myUnit.GetStat(STAT_SKILLPOINTSLEFT, 0) < static_cast<int32_t>(count)) {
        return;
    }
    std::array<uint8_t, 3> packet{};
    packet[0] = 0x3B;
    const auto skill16 = static_cast<uint16_t>(skill);
    std::memcpy(packet.data() + 1, &skill16, sizeof(uint16_t));
    for (uint32_t i = 0; i < count; ++i) {
        d2net::CLIENT_Send(packet.size(), 1, packet.data());
        if (i + 1 != count) {
            std::this_thread::sleep_for(std::chrono::milliseconds{500});
        }
    }
}

void TakeScreenshot() {
    GameThread::Execute([]() { d2win::D2WIN_TakeScreenshot(); });
}

// === Item Actions ===

// --- ClickItem family --------------------------------------------------------
//
// All three Click* functions share this prelude, handled internally:
//   1. Check TransactionDialog / TransactionDialogs / TransactionDialogs_2.
//      If any are non-zero, return ClickResult::TransactionInProgress.
//   2. Reset d2client::gCursorHover->x = d2client::gCursorHover->y = 0xFFFFFFFF.
//   3. Acquire Bridge::Lock() (ref uses AutoCriticalRoom).
//
// After the prelude, each method performs its specific dispatch. Return
// values are used by the JS binding to reproduce ref's per-arg-shape rval
// table exactly (see GameFunctions.cpp clickItem for the mapping).
//
// Reference: JSGame.cpp my_clickItem (lines 357-661).

ClickResult ClickBodyLocation(BodyLocation slot, InventoryOwner owner) {
    if (*d2client::gpTransactionDialog != nullptr || *d2client::gnTransactionDialogs != 0 ||
        *d2client::gnTransactionDialogs_2 != 0) {
        return ClickResult::TransactionInProgress;
    }
    d2client::gCursorHover->x = -1;
    d2client::gCursorHover->y = -1;
    auto guard = Bridge::Lock();

    const auto slotIdx = static_cast<uint32_t>(slot);
    if (owner == InventoryOwner::Player) {
        if (slotIdx < 1 || slotIdx >= d2client::gaBodyClickTable->size()) {
            return ClickResult::InvalidTarget;
        }
        return GameThread::Execute([slotIdx]() -> ClickResult {
            auto* player = d2client::UNITS_GetPlayerUnit();
            if (player == nullptr) {
                return ClickResult::InvalidTarget;
            }
            // BodyClickTable is a function pointer table: each entry is a
            // void __fastcall (D2UnitStrc*, D2InventoryStrc*, int).
            auto& table = *d2client::gaBodyClickTable;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounds checked above
            auto* fn = table[slotIdx];
            if (fn == nullptr) {
                return ClickResult::InvalidTarget;
            }
            fn(player, player->pInventory, static_cast<int32_t>(slotIdx));
            return ClickResult::Dispatched;
        });
    }

    // owner == Mercenary
    if (slot != BodyLocation::Head && slot != BodyLocation::Body && slot != BodyLocation::RightPrimary) {
        return ClickResult::InvalidTarget;
    }
    return GameThread::Execute([slotIdx]() -> ClickResult {
        // MercItemAction(0x61, slot) is the single dispatch the reference's
        // helper ultimately reaches; the merc-presence walk it does first is
        // redundant with the action's own validity check.
        d2client::MERCENARY_ItemAction(0x61, slotIdx);
        return ClickResult::Dispatched;
    });
}

// Reference JSGame.cpp:467-554 -- `clickItem(button, unit)`. Dispatches based
// on the item's current location:
//   Equip                 -> BodyClickTable[item->pItemData->nBodyLoc] (toggle equip)
//   Inventory/Stash/Cube  -> grid -> pixel via container layout, LeftClickItem_I or ClickItemRight
//   Belt                  -> screen coords from belt slot, ClickBelt or ClickBeltRight
//   else (Ground, Trade)  -> InvalidTarget
ClickResult ClickItem(ClickButton button, const Unit& item) {
    if (*d2client::gpTransactionDialog != nullptr || *d2client::gnTransactionDialogs != 0 ||
        *d2client::gnTransactionDialogs_2 != 0) {
        return ClickResult::TransactionInProgress;
    }
    if (!item) {
        return ClickResult::InvalidTarget;
    }
    if (item.Type() != UnitType::Item) {
        return ClickResult::NotAnItem;
    }

    // Resolve the framework Unit handle to its raw D2UnitStrc -- we need
    // pItemData->nBodyLoc / nInvPage and pItemPath grid coords directly.
    auto* itemPtr = d2client::UNITS_GetServerSideUnit(item.Id(), UnitType::Item);
    if (itemPtr == nullptr) {
        itemPtr = d2client::UNITS_GetClientSideUnit(item.Id(), UnitType::Item);
    }
    if (itemPtr == nullptr || itemPtr->pItemData == nullptr || itemPtr->pStaticPath == nullptr) {
        return ClickResult::InvalidTarget;
    }

    const auto location = static_cast<ItemLocation>(itemPtr->pItemData->pExtraData.nNodePos);
    // Items store their grid position (or belt slot index, for belt items) in
    // the static-path's game coords. Reference reads `pItemPath->dwPosX/Y`;
    // D2MOO names the union variant `pStaticPath` and the coord pair `tGameCoords.nX/nY`.
    const auto gridX = itemPtr->pStaticPath->tGameCoords.nX;
    const auto gridY = itemPtr->pStaticPath->tGameCoords.nY;

    d2client::gCursorHover->x = gridX;
    d2client::gCursorHover->y = gridY;
    auto guard = Bridge::Lock();

    // Reference JSGame.cpp:483-493 -- when button == 4, short-circuit before
    // the location dispatch and route merc-owned items through MercItemAction.
    // Items not owned by the merc yield a no-op (still consumes the click and
    // returns success -- matches reference's JSVAL_TRUE-without-action path).
    if (button == ClickButton::Mercenary) {
        auto player = Unit::Player();
        if (!player) {
            return ClickResult::InvalidTarget;
        }
        auto merc = player.FindMerc();
        if (!merc) {
            return ClickResult::InvalidTarget;
        }
        const auto mercId = merc->Id();
        if (itemPtr->pItemData->pExtraData.pParentInv == nullptr ||
            itemPtr->pItemData->pExtraData.pParentInv->pOwner == nullptr ||
            itemPtr->pItemData->pExtraData.pParentInv->pOwner->dwUnitId != mercId) {
            return ClickResult::InvalidTarget;
        }
        const uint32_t bodyLoc = itemPtr->pItemData->nBodyLoc;
        return GameThread::Execute([bodyLoc]() -> ClickResult {
            d2client::MERCENARY_ItemAction(0x61, bodyLoc);
            return ClickResult::Dispatched;
        });
    }

    // Reference JSG:540-554 -- when the click target is the cursor item, the
    // `button` arg is reinterpreted as a body-slot index (1..10) and dispatched
    // through BodyClickTable to equip the held item. Scripts pass the slot via
    // the same arg the framework casts to ClickButton; values 5..10 fall outside
    // the named enum and reach this branch as their raw ordinal.
    if (d2common::INVENTORY_GetCursorItem() == itemPtr) {
        const auto slot = static_cast<uint32_t>(button);
        if (slot < 1 || slot >= d2client::gaBodyClickTable->size()) {
            return ClickResult::InvalidTarget;
        }
        return GameThread::Execute([slot]() -> ClickResult {
            auto* player = d2client::UNITS_GetPlayerUnit();
            if (player == nullptr || player->pInventory == nullptr) {
                return ClickResult::InvalidTarget;
            }
            auto& table = *d2client::gaBodyClickTable;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - slot bounds checked above
            auto* fn = table[slot];
            if (fn == nullptr) {
                return ClickResult::InvalidTarget;
            }
            fn(player, player->pInventory, static_cast<int32_t>(slot));
            return ClickResult::Dispatched;
        });
    }

    if (location == ItemLocation::Equip) {
        // Reference JSG:417-424 -- equip click via BodyClickTable[bodyLoc].
        const uint32_t bodyLoc = itemPtr->pItemData->nBodyLoc;
        return GameThread::Execute([bodyLoc]() -> ClickResult {
            auto* player = d2client::UNITS_GetPlayerUnit();
            if (player == nullptr || player->pInventory == nullptr) {
                return ClickResult::InvalidTarget;
            }
            if (bodyLoc < 1 || bodyLoc >= d2client::gaBodyClickTable->size()) {
                return ClickResult::InvalidTarget;
            }
            auto& table = *d2client::gaBodyClickTable;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bodyLoc bounds checked above
            auto* fn = table[bodyLoc];
            if (fn == nullptr) {
                return ClickResult::InvalidTarget;
            }
            fn(player, player->pInventory, static_cast<int32_t>(bodyLoc));
            return ClickResult::Dispatched;
        });
    }

    if (location == ItemLocation::Belt) {
        // gridX is the belt slot index 0..15.
        if (gridX < 0 || gridX > 15) {
            return ClickResult::InvalidTarget;
        }
        const uint32_t slotIndex = static_cast<uint32_t>(gridX);
        const uint32_t col = slotIndex % 4;
        const uint32_t row = slotIndex / 4;
        const auto screenSize = d2gfx::D2GFX_GetResolutionMode();
        const int32_t baseX = (screenSize == 2) ? 440 : 360;
        const int32_t baseY = (screenSize == 2) ? 580 : 460;
        const int32_t beltX = baseX + (static_cast<int32_t>(col) * 29);
        const int32_t beltY = baseY - (static_cast<int32_t>(row) * 29);

        return GameThread::Execute([button, beltX, beltY, slotIndex]() -> ClickResult {
            auto* player = d2client::UNITS_GetPlayerUnit();
            if (player == nullptr || player->pInventory == nullptr) {
                return ClickResult::InvalidTarget;
            }
            switch (button) {
                case ClickButton::Left:
                    asm_thunks::ClickBelt(beltX, beltY, player->pInventory);
                    return ClickResult::Dispatched;
                case ClickButton::Right:
                    asm_thunks::ClickBeltRight(player->pInventory, player, false, slotIndex);
                    return ClickResult::Dispatched;
                case ClickButton::ShiftLeft:
                    // Reference JSG:649-650 -- shift-left on belt routes through
                    // ClickBeltRight with HoldShift=1 (potion-use pathway).
                    asm_thunks::ClickBeltRight(player->pInventory, player, true, slotIndex);
                    return ClickResult::Dispatched;
                default:
                    return ClickResult::InvalidTarget;
            }
        });
    }

    if (location != ItemLocation::Inventory && location != ItemLocation::Stash && location != ItemLocation::Cube) {
        return ClickResult::InvalidTarget;
    }

    return GameThread::Execute([button, gridX, gridY, location]() -> ClickResult {
        auto* player = d2client::UNITS_GetPlayerUnit();
        if (player == nullptr || player->pInventory == nullptr) {
            return ClickResult::InvalidTarget;
        }
        const auto entry = ResolveContainerLayout(location);
        if (!entry) {
            return ClickResult::InvalidTarget;
        }
        const auto px = entry->layout->nGridLeft + (gridX * entry->layout->nGridBoxWidth) + 10;
        const auto py = entry->layout->nGridTop + (gridY * entry->layout->nGridBoxHeight) + 10;

        switch (button) {
            case ClickButton::Left:
                asm_thunks::LeftClickItem(entry->locationCode, player, player->pInventory, px, py, 1U, entry->layout);
                return ClickResult::Dispatched;
            case ClickButton::ShiftLeft:
                asm_thunks::LeftClickItem(entry->locationCode, player, player->pInventory, px, py, 5U, entry->layout);
                return ClickResult::Dispatched;
            case ClickButton::Right:
                asm_thunks::ClickItemRight(px, py, entry->locationCode, player, player->pInventory);
                return ClickResult::Dispatched;
            default:
                return ClickResult::InvalidTarget;
        }
    });
}

ClickResult ClickContainerSlot(ClickButton button, Position gridPos, ItemLocation container) {
    if (*d2client::gpTransactionDialog != nullptr || *d2client::gnTransactionDialogs != 0 ||
        *d2client::gnTransactionDialogs_2 != 0) {
        return ClickResult::TransactionInProgress;
    }
    d2client::gCursorHover->x = static_cast<int32_t>(gridPos.x);
    d2client::gCursorHover->y = static_cast<int32_t>(gridPos.y);
    auto guard = Bridge::Lock();

    if (container == ItemLocation::Belt) {
        // Reference JSGame.cpp:617-650. Belt is a 4x4 grid (16 slots). The
        // Belt[] table maps slot index -> (col, row) so slot = row*4 + col.
        // Screen coords are computed per-resolution; the slot index is what
        // ClickBeltRight ultimately consumes (`dwPotPos`).
        if (gridPos.x >= 4U || gridPos.y >= 4U) {
            return ClickResult::InvalidTarget;
        }
        const uint32_t slotIndex = (gridPos.y * 4U) + gridPos.x;
        const auto screenSize = d2gfx::D2GFX_GetResolutionMode();
        const int32_t baseX = (screenSize == 2) ? 440 : 360;
        const int32_t baseY = (screenSize == 2) ? 580 : 460;
        const int32_t beltX = baseX + (static_cast<int32_t>(gridPos.x) * 29);
        const int32_t beltY = baseY - (static_cast<int32_t>(gridPos.y) * 29);

        return GameThread::Execute([button, beltX, beltY, slotIndex]() -> ClickResult {
            auto* player = d2client::UNITS_GetPlayerUnit();
            if (player == nullptr || player->pInventory == nullptr) {
                return ClickResult::InvalidTarget;
            }
            switch (button) {
                case ClickButton::Left:
                    asm_thunks::ClickBelt(beltX, beltY, player->pInventory);
                    return ClickResult::Dispatched;
                case ClickButton::Right:
                    asm_thunks::ClickBeltRight(player->pInventory, player, false, slotIndex);
                    return ClickResult::Dispatched;
                case ClickButton::ShiftLeft:
                    // Reference JSG:649-650 -- shift-left on belt routes through
                    // ClickBeltRight with HoldShift=1 (potion-use pathway).
                    asm_thunks::ClickBeltRight(player->pInventory, player, true, slotIndex);
                    return ClickResult::Dispatched;
                default:
                    return ClickResult::InvalidTarget;
            }
        });
    }

    if (container != ItemLocation::Inventory && container != ItemLocation::Trade && container != ItemLocation::Cube &&
        container != ItemLocation::Stash) {
        return ClickResult::InvalidTarget;
    }

    return GameThread::Execute([button, gridPos, container]() -> ClickResult {
        auto* player = d2client::UNITS_GetPlayerUnit();
        if (player == nullptr || player->pInventory == nullptr) {
            return ClickResult::InvalidTarget;
        }
        const auto entry = ResolveContainerLayout(container);
        if (!entry) {
            return ClickResult::InvalidTarget;
        }
        // Reference JSGame.cpp:569-581 -- when placing a multi-cell cursor item,
        // the click pixel must land inside the item's footprint, not on its
        // top-left corner. Bump grid coords by +1 in any axis where the cursor
        // item is wider/taller than 1 cell. The pre-nudge coords are already
        // mirrored to CursorHover above, matching reference's order.
        auto gx = static_cast<int32_t>(gridPos.x);
        auto gy = static_cast<int32_t>(gridPos.y);
        if (auto* cursor = d2common::INVENTORY_GetCursorItem(); cursor != nullptr) {
            auto* txt = d2common::DATATBLS_GetItemsTxtRecord(cursor->dwClassId);
            if (txt != nullptr) {
                if (txt->nInvHeight > 1) {
                    gy += 1;
                }
                if (txt->nInvWidth > 1) {
                    gx += 1;
                }
            }
        }
        const auto px = entry->layout->nGridLeft + (gx * entry->layout->nGridBoxWidth) + 10;
        const auto py = entry->layout->nGridTop + (gy * entry->layout->nGridBoxHeight) + 10;

        switch (button) {
            case ClickButton::Left:
                asm_thunks::LeftClickItem(entry->locationCode, player, player->pInventory, px, py, 1U, entry->layout);
                return ClickResult::Dispatched;
            case ClickButton::ShiftLeft:
                asm_thunks::LeftClickItem(entry->locationCode, player, player->pInventory, px, py, 5U, entry->layout);
                return ClickResult::Dispatched;
            case ClickButton::Right:
                // Reference D2Helpers.cpp:800 -- ClickItemRight_ASM expects
                // (x, y, location, pPlayer, pInventory).
                asm_thunks::ClickItemRight(px, py, entry->locationCode, player, player->pInventory);
                return ClickResult::Dispatched;
            default:
                return ClickResult::InvalidTarget;
        }
    });
}

// Reference JSGame.cpp:1074-1140 my_clickParty -- dispatches on the JS mode
// arg, with safety gates:
//   AllowLoot (0)  -> HostilePartyUnit(roster, 2)   -- only on hardcore
//   Unhostile (1)  -> HostilePartyUnit(roster, 1)
//   Hostile   (4)  -> HostilePartyUnit(roster, 3)
//   HostileAlt(5)  -> HostilePartyUnit(roster, 4)
//   Invite    (2)  -> ClickParty(roster, mode)      -- refused if already partied
//   Leave     (3)  -> LeaveParty                    -- only when target has a party
// All modes refuse self-click (target dwUnitId matches the player's). The
// `mode` arg of ClickParty is unused in 1.14d. HostilePartyUnit goes through
// the naked thunk; ClickParty is a regular fastcall import (
// IDA decompile shows it's __thiscall).
void ClickPartyMember(const Party& party, PartyMode mode) {
    if (party.Id() == 0) {
        return;
    }
    auto* myUnit = d2client::UNITS_GetPlayerUnit();
    if (myUnit == nullptr) {
        return;
    }
    // Resolve the framework Party handle to its raw RosterUnit pointer, plus
    // the player's own roster entry (the head of the list) so we can compare
    // wPartyId fields. Walk under the read lock and copy the fields we need
    // before releasing it.
    constexpr uint16_t NO_PARTY = std::numeric_limits<uint16_t>::max();
    D2RosterUnitStrc* rosterPtr = nullptr;
    uint16_t targetPartyId = NO_PARTY;
    uint16_t myPartyId = NO_PARTY;
    uint32_t targetUnitId = 0;
    {
        auto guard = Bridge::Lock();
        auto* myRoster = *d2client::gpPlayerUnitList;
        if (myRoster == nullptr) {
            return;
        }
        myPartyId = myRoster->wPartyId;
        for (auto* scan = myRoster; scan != nullptr; scan = scan->pNext) {
            if (scan->dwUnitId == party.Id()) {
                rosterPtr = scan;
                targetPartyId = scan->wPartyId;
                targetUnitId = scan->dwUnitId;
                break;
            }
        }
    }
    if (rosterPtr == nullptr) {
        return;
    }
    // Refuse self-click -- this can crash on older builds.
    if (targetUnitId == myUnit->dwUnitId) {
        return;
    }

    switch (mode) {
        case PartyMode::AllowLoot: {
            // Loot only applies on hardcore.
            auto* data = *d2launch::gpBnetData;
            if (data == nullptr || (data->nCharFlags & CHAR_FLAG_HARDCORE) == 0) {
                return;
            }
            asm_thunks::HostilePartyUnit(rosterPtr, 2U);
            return;
        }
        case PartyMode::Unhostile:
            asm_thunks::HostilePartyUnit(rosterPtr, 1U);
            return;
        case PartyMode::Hostile:
            asm_thunks::HostilePartyUnit(rosterPtr, 3U);
            return;
        case PartyMode::HostileAlt:
            asm_thunks::HostilePartyUnit(rosterPtr, 4U);
            return;
        case PartyMode::Invite:
            // Already partied with the target -- refuse re-invite.
            if (targetPartyId != NO_PARTY && myPartyId == targetPartyId) {
                return;
            }
            d2client::ClickParty_I(rosterPtr);
            return;
        case PartyMode::Leave:
            // Only leave a party if the target actually has one.
            if (targetPartyId == NO_PARTY) {
                return;
            }
            d2client::PARTY_Leave();
            return;
    }
}

void LeaveParty() {
    d2client::PARTY_Leave();
}

uint32_t CheckUnitCollision(const game::Unit& unit1, const game::Unit& unit2, uint32_t mask) {
    const auto id1 = unit1.Id();
    const auto type1 = unit1.Type();
    const auto id2 = unit2.Id();
    const auto type2 = unit2.Type();
    auto* p1 = d2client::UNITS_GetServerSideUnit(id1, type1);
    if (p1 == nullptr) {
        p1 = d2client::UNITS_GetClientSideUnit(id1, type1);
    }
    auto* p2 = d2client::UNITS_GetServerSideUnit(id2, type2);
    if (p2 == nullptr) {
        p2 = d2client::UNITS_GetClientSideUnit(id2, type2);
    }
    if (p1 == nullptr || p2 == nullptr) {
        return 0;
    }
    return d2common::UNITS_TestCollisionWithUnit(p1, p2, mask);
}

// === Skill Name Tables ===

struct SkillEntry {
    std::string_view name;
    int32_t id;
};

// 1.14d skill table extracted from reference/d2bs/D2Skills.h (216 entries).
// Future game versions could read from game data files or memory instead.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
static constexpr SkillEntry SKILLS_1_14D[] = {
    {.name = "Attack", .id = 0},
    {.name = "Kick", .id = 1},
    {.name = "Throw", .id = 2},
    {.name = "Unsummon", .id = 3},
    {.name = "Left Hand Throw", .id = 4},
    {.name = "Left Hand Swing", .id = 5},
    {.name = "Magic Arrow", .id = 6},
    {.name = "Fire Arrow", .id = 7},
    {.name = "Inner Sight", .id = 8},
    {.name = "Critical Strike", .id = 9},
    {.name = "Jab", .id = 10},
    {.name = "Cold Arrow", .id = 11},
    {.name = "Multiple Shot", .id = 12},
    {.name = "Dodge", .id = 13},
    {.name = "Power Strike", .id = 14},
    {.name = "Poison Javelin", .id = 15},
    {.name = "Exploding Arrow", .id = 16},
    {.name = "Slow Missiles", .id = 17},
    {.name = "Avoid", .id = 18},
    {.name = "Impale", .id = 19},
    {.name = "Lightning Bolt", .id = 20},
    {.name = "Ice Arrow", .id = 21},
    {.name = "Guided Arrow", .id = 22},
    {.name = "Penetrate", .id = 23},
    {.name = "Charged Strike", .id = 24},
    {.name = "Plague Javelin", .id = 25},
    {.name = "Strafe", .id = 26},
    {.name = "Immolation Arrow", .id = 27},
    {.name = "Decoy", .id = 28},
    {.name = "Evade", .id = 29},
    {.name = "Fend", .id = 30},
    {.name = "Freezing Arrow", .id = 31},
    {.name = "Valkyrie", .id = 32},
    {.name = "Pierce", .id = 33},
    {.name = "Lightning Strike", .id = 34},
    {.name = "Lightning Fury", .id = 35},
    {.name = "Fire Bolt", .id = 36},
    {.name = "Warmth", .id = 37},
    {.name = "Charged Bolt", .id = 38},
    {.name = "Ice Bolt", .id = 39},
    {.name = "Frozen Armor", .id = 40},
    {.name = "Inferno", .id = 41},
    {.name = "Static Field", .id = 42},
    {.name = "Telekinesis", .id = 43},
    {.name = "Frost Nova", .id = 44},
    {.name = "Ice Blast", .id = 45},
    {.name = "Blaze", .id = 46},
    {.name = "Fire Ball", .id = 47},
    {.name = "Nova", .id = 48},
    {.name = "Lightning", .id = 49},
    {.name = "Shiver Armor", .id = 50},
    {.name = "Fire Wall", .id = 51},
    {.name = "Enchant", .id = 52},
    {.name = "Chain Lightning", .id = 53},
    {.name = "Teleport", .id = 54},
    {.name = "Glacial Spike", .id = 55},
    {.name = "Meteor", .id = 56},
    {.name = "Thunder Storm", .id = 57},
    {.name = "Energy Shield", .id = 58},
    {.name = "Blizzard", .id = 59},
    {.name = "Chilling Armor", .id = 60},
    {.name = "Fire Mastery", .id = 61},
    {.name = "Hydra", .id = 62},
    {.name = "Lightning Mastery", .id = 63},
    {.name = "Frozen Orb", .id = 64},
    {.name = "Cold Mastery", .id = 65},
    {.name = "Amplify Damage", .id = 66},
    {.name = "Teeth", .id = 67},
    {.name = "Bone Armor", .id = 68},
    {.name = "Skeleton Mastery", .id = 69},
    {.name = "Raise Skeleton", .id = 70},
    {.name = "Dim Vision", .id = 71},
    {.name = "Weaken", .id = 72},
    {.name = "Poison Dagger", .id = 73},
    {.name = "Corpse Explosion", .id = 74},
    {.name = "Clay Golem", .id = 75},
    {.name = "Iron Maiden", .id = 76},
    {.name = "Terror", .id = 77},
    {.name = "Bone Wall", .id = 78},
    {.name = "Golem Mastery", .id = 79},
    {.name = "Raise Skeletal Mage", .id = 80},
    {.name = "Confuse", .id = 81},
    {.name = "Life Tap", .id = 82},
    {.name = "Poison Explosion", .id = 83},
    {.name = "Bone Spear", .id = 84},
    {.name = "Blood Golem", .id = 85},
    {.name = "Attract", .id = 86},
    {.name = "Decrepify", .id = 87},
    {.name = "Bone Prison", .id = 88},
    {.name = "Summon Resist", .id = 89},
    {.name = "Iron Golem", .id = 90},
    {.name = "Lower Resist", .id = 91},
    {.name = "Poison Nova", .id = 92},
    {.name = "Bone Spirit", .id = 93},
    {.name = "Fire Golem", .id = 94},
    {.name = "Revive", .id = 95},
    {.name = "Sacrifice", .id = 96},
    {.name = "Smite", .id = 97},
    {.name = "Might", .id = 98},
    {.name = "Prayer", .id = 99},
    {.name = "Resist Fire", .id = 100},
    {.name = "Holy Bolt", .id = 101},
    {.name = "Holy Fire", .id = 102},
    {.name = "Thorns", .id = 103},
    {.name = "Defiance", .id = 104},
    {.name = "Resist Cold", .id = 105},
    {.name = "Zeal", .id = 106},
    {.name = "Charge", .id = 107},
    {.name = "Blessed Aim", .id = 108},
    {.name = "Cleansing", .id = 109},
    {.name = "Resist Lightning", .id = 110},
    {.name = "Vengeance", .id = 111},
    {.name = "Blessed Hammer", .id = 112},
    {.name = "Concentration", .id = 113},
    {.name = "Holy Freeze", .id = 114},
    {.name = "Vigor", .id = 115},
    {.name = "Conversion", .id = 116},
    {.name = "Holy Shield", .id = 117},
    {.name = "Holy Shock", .id = 118},
    {.name = "Sanctuary", .id = 119},
    {.name = "Meditation", .id = 120},
    {.name = "Fist of the Heavens", .id = 121},
    {.name = "Fanaticism", .id = 122},
    {.name = "Conviction", .id = 123},
    {.name = "Redemption", .id = 124},
    {.name = "Salvation", .id = 125},
    {.name = "Bash", .id = 126},
    {.name = "Sword Mastery", .id = 127},
    {.name = "Axe Mastery", .id = 128},
    {.name = "Mace Mastery", .id = 129},
    {.name = "Howl", .id = 130},
    {.name = "Find Potion", .id = 131},
    {.name = "Leap", .id = 132},
    {.name = "Double Swing", .id = 133},
    {.name = "Pole Arm Mastery", .id = 134},
    {.name = "Throwing Mastery", .id = 135},
    {.name = "Spear Mastery", .id = 136},
    {.name = "Taunt", .id = 137},
    {.name = "Shout", .id = 138},
    {.name = "Stun", .id = 139},
    {.name = "Double Throw", .id = 140},
    {.name = "Increased Stamina", .id = 141},
    {.name = "Find Item", .id = 142},
    {.name = "Leap Attack", .id = 143},
    {.name = "Concentrate", .id = 144},
    {.name = "Iron Skin", .id = 145},
    {.name = "Battle Cry", .id = 146},
    {.name = "Frenzy", .id = 147},
    {.name = "Increased Speed", .id = 148},
    {.name = "Battle Orders", .id = 149},
    {.name = "Grim Ward", .id = 150},
    {.name = "Whirlwind", .id = 151},
    {.name = "Berserk", .id = 152},
    {.name = "Natural Resistance", .id = 153},
    {.name = "War Cry", .id = 154},
    {.name = "Battle Command", .id = 155},
    {.name = "Scroll of Townportal", .id = 219},
    {.name = "Book of Townportal", .id = 220},
    {.name = "Raven", .id = 221},
    {.name = "Poison Creeper", .id = 222},
    {.name = "Werewolf", .id = 223},
    {.name = "Shape Shifting", .id = 224},
    {.name = "Firestorm", .id = 225},
    {.name = "Oak Sage", .id = 226},
    {.name = "Summon Spirit Wolf", .id = 227},
    {.name = "Werebear", .id = 228},
    {.name = "Molten Boulder", .id = 229},
    {.name = "Arctic Blast", .id = 230},
    {.name = "Carrion Vine", .id = 231},
    {.name = "Feral Rage", .id = 232},
    {.name = "Maul", .id = 233},
    {.name = "Fissure", .id = 234},
    {.name = "Cyclone Armor", .id = 235},
    {.name = "Heart of Wolverine", .id = 236},
    {.name = "Summon Dire Wolf", .id = 237},
    {.name = "Rabies", .id = 238},
    {.name = "Fire Claws", .id = 239},
    {.name = "Twister", .id = 240},
    {.name = "Solar Creeper", .id = 241},
    {.name = "Hunger", .id = 242},
    {.name = "Shock Wave", .id = 243},
    {.name = "Volcano", .id = 244},
    {.name = "Tornado", .id = 245},
    {.name = "Spirit of barbs", .id = 246},
    {.name = "Summon Grizzly", .id = 247},
    {.name = "Fury", .id = 248},
    {.name = "Armageddon", .id = 249},
    {.name = "Hurricane", .id = 250},
    {.name = "Fire Blast", .id = 251},
    {.name = "Claw Mastery", .id = 252},
    {.name = "Psychic Hammer", .id = 253},
    {.name = "Tiger Strike", .id = 254},
    {.name = "Dragon Talon", .id = 255},
    {.name = "Shock Web", .id = 256},
    {.name = "Blade Sentinel", .id = 257},
    {.name = "Burst of Speed", .id = 258},
    {.name = "Fists of Fire", .id = 259},
    {.name = "Dragon Claw", .id = 260},
    {.name = "Charged Bolt Sentry", .id = 261},
    {.name = "Wake of Fire", .id = 262},
    {.name = "Weapon Block", .id = 263},
    {.name = "Cloak of Shadows", .id = 264},
    {.name = "Cobra Strike", .id = 265},
    {.name = "Blade Fury", .id = 266},
    {.name = "Fade", .id = 267},
    {.name = "Shadow Warrior", .id = 268},
    {.name = "Claws of Thunder", .id = 269},
    {.name = "Dragon Tail", .id = 270},
    {.name = "Lightning Sentry", .id = 271},
    {.name = "Wake of Inferno", .id = 272},
    {.name = "Mind Blast", .id = 273},
    {.name = "Blades of Ice", .id = 274},
    {.name = "Dragon Flight", .id = 275},
    {.name = "Death Sentry", .id = 276},
    {.name = "Blade Shield", .id = 277},
    {.name = "Venom", .id = 278},
    {.name = "Shadow Master", .id = 279},
    {.name = "Phoenix Strike", .id = 280},
};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

std::optional<uint16_t> GetSkillByName(std::string_view name) {
    auto iter = std::ranges::find_if(
        SKILLS_1_14D, [&](const SkillEntry& entry) { return utils::EqualsCaseInsensitive(entry.name, name); });
    if (iter != std::end(SKILLS_1_14D)) {
        return iter->id;
    }
    return std::nullopt;
}

// === IPC ===

int32_t SendIPC(uint32_t mode, std::string_view data, uintptr_t targetHwnd, std::string_view windowClassName,
                std::string_view windowName) {
    HWND target = reinterpret_cast<HWND>(targetHwnd);
    if (target == nullptr) {
        std::wstring classWide;
        std::wstring nameWide;
        const wchar_t* classPtr = nullptr;
        const wchar_t* namePtr = nullptr;
        if (!windowClassName.empty()) {
            classWide = utils::ToWStr(std::string(windowClassName), CP_UTF8);
            classPtr = classWide.c_str();
        }
        if (!windowName.empty()) {
            nameWide = utils::ToWStr(std::string(windowName), CP_UTF8);
            namePtr = nameWide.c_str();
        }
        // Both empty -> FindWindowW(NULL, NULL) returns the broadcast HWND
        // (per reference behaviour the recipient is the foreground window).
        target = ::FindWindowW(classPtr, namePtr);
    }
    if (target == nullptr) {
        return 0;
    }
    // string_view::data() need not be NUL-terminated; copy to ensure it is.
    const std::string buffer{data};
    COPYDATASTRUCT cds{};
    cds.dwData = mode;
    cds.cbData = static_cast<DWORD>(buffer.size()) + 1U;  // include null terminator
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - COPYDATASTRUCT.lpData isn't mutated
    cds.lpData = const_cast<char*>(buffer.c_str());
    return static_cast<int32_t>(::SendMessageW(target, WM_COPYDATA, reinterpret_cast<WPARAM>(d2gfx::WINDOW_GetWindow()),
                                               reinterpret_cast<LPARAM>(&cds)));
}

// === Misc ===

void PlayGameSound(uint32_t soundId) {
    if (!WaitForGameReady()) {
        return;
    }
    GameThread::Execute([soundId]() {
        auto* player = *d2client::gpPlayerUnit;
        if (player == nullptr) {
            return;
        }
        d2client::SOUND_PlaySound(soundId, player, 0, 0, 0);
    });
}

void* GetHwnd() {
    return d2gfx::WINDOW_GetWindow();
}

// Reference Game.cpp:8-12 SendGold. Drop and stash both write the same global
// pair and trigger the gold-dialog action; the difference lives in the
// dialog's mode value.
void GoldAction(GoldActionMode mode, int32_t amount) {
    if (!WaitForGameReady()) {
        return;
    }
    GameThread::Execute([mode, amount]() {
        *d2client::gnGoldDialogAmount = amount;
        *d2client::gnGoldDialogAction = static_cast<int32_t>(mode);
        d2client::UI_PerformGoldDialogAction();
    });
}

// Reference JSGame.cpp:1421-1454 -- packet 0x59 carrying a fully-typed unit
// reference plus target coords. The reference gates this behind a
// configuration flag (`bEnableUnsupported`) since cooperative servers reject
// the move; we leave the gating to the JS layer and just emit the packet.
//
// D2MOO does not define a typed struct for this packet -- the only packet
// typings it ships are framing types in `D2Net/include/Packet.h`. Build it as
// a packed byte buffer.
void MoveNPC(uint32_t npcId, Position pos) {
    if (!WaitForGameReady()) {
        return;
    }
    std::array<uint8_t, 17> packet{};
    packet[0] = 0x59;
    constexpr uint32_t UNIT_TYPE_NPC = 1;
    std::memcpy(packet.data() + 1, &UNIT_TYPE_NPC, sizeof(uint32_t));
    std::memcpy(packet.data() + 5, &npcId, sizeof(uint32_t));
    std::memcpy(packet.data() + 9, &pos.x, sizeof(uint32_t));
    std::memcpy(packet.data() + 13, &pos.y, sizeof(uint32_t));
    d2net::CLIENT_Send(packet.size(), 1, packet.data());
}

// Reference Room.cpp:5-49 RevealRoom -- for each room: AddRoomData when pRoom1
// is null (forces the room to load on demand for distant areas), reveal it,
// then RemoveRoomData if we were the ones to allocate it. Cross-level reveals
// switch the AutomapLayer to the target's layer for the duration and restore
// the player's own layer on exit. The layer save/restore is essential: scripts
// that reveal a non-current level would otherwise corrupt the player's map.
bool RevealLevel(uint32_t levelNo, bool drawPresets) {
    if (!WaitForGameReady()) {
        return false;
    }
    return GameThread::Execute([levelNo, drawPresets]() -> bool {
        auto* player = d2client::UNITS_GetPlayerUnit();
        // `D2UnitStrc::pDrlgAct` is typed as D2MOO's `::D2DrlgActStrc*`; the bytes follow
        // the 1.14d allocation modeled by `extras::D2DrlgActStrc`.
        auto* act = (player != nullptr) ? reinterpret_cast<D2DrlgActStrc*>(player->pDrlgAct) : nullptr;
        if (act == nullptr || act->pDrlg == nullptr) {
            return false;
        }
        D2DrlgLevelStrc* target = nullptr;
        for (auto* level = act->pDrlg->pLevel; level != nullptr; level = level->pNextLevel) {
            if (static_cast<uint32_t>(level->nLevelId) == levelNo) {
                target = level;
                break;
            }
        }
        if (target == nullptr) {
            return false;
        }
        if (target->pFirstRoomEx == nullptr) {
            d2common::DRLG_InitLevel(target);
        }

        // If the target is a different level than the player's current one,
        // the AutomapLayer must be repointed before we draw into it. The
        // saved layer is restored after the reveal loop. Reference Room.cpp:9
        // grabs the player's level via the player's path chain.
        auto* pathRoom = player->pDynamicPath != nullptr
                             ? reinterpret_cast<D2ActiveRoomStrc*>(player->pDynamicPath->pRoom)
                             : nullptr;
        const auto* playerLevel =
            (pathRoom != nullptr && pathRoom->pDrlgRoom != nullptr) ? pathRoom->pDrlgRoom->pLevel : nullptr;
        const uint32_t playerLevelNo = (playerLevel != nullptr) ? static_cast<uint32_t>(playerLevel->nLevelId) : 0U;
        const bool isOtherLevel = (playerLevelNo != 0U && playerLevelNo != levelNo);
        if (isOtherLevel) {
            *d2client::gpAutomapLayer = asm_thunks::InitAutomapLayerForLevel(levelNo);
        }

        for (auto* room = target->pFirstRoomEx; room != nullptr; room = room->pDrlgRoomNext) {
            // RAII: AddRoomData when pRoom is null, RemoveRoomData on dtor if
            // we were the ones to allocate. The guard re-checks pRoom on the
            // game thread to avoid a TOCTOU race with concurrent script
            // threads observing the same null pRoom.
            RoomDataGuard guard(room);
            if (room->pRoom == nullptr) {
                continue;  // AddRoomData failed -- skip this room.
            }
            d2client::AUTOMAP_RevealRoom(room->pRoom, 1, *d2client::gpAutomapLayer);
            if (drawPresets) {
                DrawPresetsForRoom(room);
            }
        }

        if (isOtherLevel) {
            asm_thunks::InitAutomapLayerForLevel(playerLevelNo);
        }
        return true;
    });
}

// === NPC Interaction ===

bool IsScrollingText() {
    if (!WaitForGameReady()) {
        return false;
    }

    // Walk Storm.dll's WM_LBUTTONDOWN handler list for the game window. While
    // an NPC dialog is scrolling in, the click handler is `D2CLIENT_CloseNPCTalk`
    // (clicking-through cuts the scroll short); once scrolling finishes, the
    // game swaps it for the dialog-line click handler.
    HWND d2Hwnd = d2gfx::WINDOW_GetWindow();
    auto* whht = storm::gWindowHandlers.Ptr();
    if (whht == nullptr || whht->pTable == nullptr || whht->dwLength == 0) {
        return false;
    }

    const auto closeNpcTalkAddr = reinterpret_cast<uintptr_t>(d2client::UI_CloseNPCTalk.Ptr());
    constexpr uint32_t MAGIC_GMSG = 0x534D5347;
    const uint32_t windowBucket = (MAGIC_GMSG ^ reinterpret_cast<uint32_t>(d2Hwnd)) % whht->dwLength;

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) - hash table indexing
    auto* whl = whht->pTable[windowBucket];
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    while (whl != nullptr) {
        if (whl->dwMagic == MAGIC_GMSG && whl->hWnd == d2Hwnd) {
            auto* mhht = whl->pMsgHandlers;
            if (mhht != nullptr && mhht->pTable != nullptr && mhht->dwLength != 0) {
                constexpr uint32_t WM_LBUTTONDOWN_MSG = 0x201;
                // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) - hash table indexing
                auto* mhl = mhht->pTable[WM_LBUTTONDOWN_MSG % mhht->dwLength];
                // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                while (mhl != nullptr) {
                    if (mhl->dwMessage != 0 && mhl->dwUnk4 < std::numeric_limits<uint32_t>::max() &&
                        reinterpret_cast<uintptr_t>(mhl->pfHandler) == closeNpcTalkAddr) {
                        return true;
                    }
                    mhl = mhl->pNext;
                }
            }
        }
        whl = whl->pNext;
    }

    return false;
}

void Cancel(CancelMode mode) {
    if (!IsInGame()) {
        return;
    }
    GameThread::Execute([mode]() {
        // CloseInteract / CloseNPCInteract toggle the automap as a side
        // effect; capture and restore so the caller's automap state is
        // preserved across the cancel.
        const auto savedAutomap = *d2client::gbAutomapOn;
        switch (mode) {
            case CancelMode::CloseInteract:
                d2client::UI_CloseInteract();
                break;
            case CancelMode::ClearCursor:
                // Reference JSUnit.cpp:756 -- drop the held cursor item via a
                // ClickMap with the always-run flag (0x08); the (10, 10) coords
                // and 0x08 flag together select the cursor-drop code path
                // inside D2CLIENT_ClickMap.
                d2client::UI_ClickMap(0, 10, 10, 0x08);
                break;
            case CancelMode::CloseNPC:
                d2client::UI_CloseNPCInteract();
                break;
            case CancelMode::ClearScreen:
                d2client::UI_ClearScreen();
                break;
            default:
                // Auto-detect: framework binding passes a negative sentinel
                // (cast of -1) when unit.cancel() is invoked with no args.
                // Reference JSUnit.cpp:742-749 picks the action by what's
                // currently visible on screen. Order matters: scrolling
                // dialog text wins over an open NPC, which wins over a held
                // item, which wins over generic interact panels.
                if (IsScrollingText()) {
                    d2client::UI_ClearScreen();
                } else if (d2client::UI_GetInteractingNPC() != nullptr) {
                    d2client::UI_CloseNPCInteract();
                } else if (d2common::INVENTORY_GetCursorItem() != nullptr) {
                    d2client::UI_ClickMap(0, 10, 10, 0x08);
                } else {
                    d2client::UI_CloseInteract();
                }
                break;
        }
        *d2client::gbAutomapOn = savedAutomap;
    });
}

// === Dialog ===

std::vector<DialogLine> GetDialogLines() {
    auto guard = Bridge::Lock();
    auto* tdi = *d2client::gpTransactionDialogsInfo;
    if (tdi == nullptr) {
        return {};
    }
    std::vector<DialogLine> out;
    const auto count = std::min<uint32_t>(tdi->dwNumLines, static_cast<uint32_t>(tdi->aDialogLines.size()));
    out.reserve(count);
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by count above
    for (uint32_t i = 0; i < count; ++i) {
        const auto& line = tdi->aDialogLines[i];
        // wcsnlen, not wcslen -- D2 doesn't always null-terminate the 120-char
        // text buffer, and the unbounded scan walks off into the next page.
        std::wstring lineWide{line.wszText.data(), wcsnlen(line.wszText.data(), line.wszText.size())};
        out.push_back({.text = utils::ToStr(lineWide, CP_UTF8), .isSelectable = line.bIsSelectable != 0});
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return out;
}

bool SelectDialogLineByText(const std::string& text) {
    // The line handler is a __stdcall that mutates D2's UI state, so post
    // it to the game thread. GameThread::Execute blocks until the handler
    // runs, matching reference's synchronous-on-script-thread call site
    // semantics from the script's perspective.
    return GameThread::Execute([&text]() -> bool {
        if (!d2client::gpTransactionDialogsInfo.IsResolved() || *d2client::gpTransactionDialogsInfo == nullptr) {
            return false;
        }
        auto* info = *d2client::gpTransactionDialogsInfo;
        const auto count = std::min<uint32_t>(info->dwNumLines, static_cast<uint32_t>(info->aDialogLines.size()));
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by count above
        for (uint32_t i = 0; i < count; ++i) {
            auto& line = info->aDialogLines[i];
            // Use the same wchar_t -> UTF-8 path as GetDialogLines so the
            // text we compare against is byte-identical to what the script
            // saw. D2 doesn't always null-terminate the 120-char buffer, so
            // use wcsnlen rather than relying on the wstring(const wchar_t*)
            // constructor's wcslen -- that scans past the array and AVs on
            // the next page when there's no terminator.
            std::wstring lineWide{line.wszText.data(), wcsnlen(line.wszText.data(), line.wszText.size())};
            auto lineUtf8 = d2bs::utils::ToStr(lineWide, CP_UTF8);
            if (lineUtf8 != text) {
                continue;
            }
            // Reference my_clickDialog gates on bMaybeSelectable (==
            // isSelectable here). Defence in depth on the handler pointer.
            if (line.bIsSelectable == 0 || line.pfHandler == nullptr) {
                return false;
            }
            line.pfHandler();
            return true;
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
        return false;
    });
}

// === Input Simulation ===

void SendClick(Point pos) {
    auto hwnd = d2gfx::WINDOW_GetWindow();
    if (hwnd == nullptr) {
        return;
    }
    constexpr auto SETTLE_DELAY = std::chrono::milliseconds{100};
    const LPARAM lp = static_cast<LPARAM>(pos.x) | (static_cast<LPARAM>(pos.y) << 16);
    hooks::PostInjectedInput(hwnd, WM_LBUTTONDOWN, 0, lp);
    std::this_thread::sleep_for(SETTLE_DELAY);
    hooks::PostInjectedInput(hwnd, WM_LBUTTONUP, 0, lp);
    // Trailing pause prevents tight script loops from racing the game's input pump.
    std::this_thread::sleep_for(SETTLE_DELAY);
}

void SendKey(uint32_t key) {
    auto hwnd = d2gfx::WINDOW_GetWindow();
    if (hwnd == nullptr) {
        return;
    }
    constexpr auto SETTLE_DELAY = std::chrono::milliseconds{100};
    LPARAM lpDown = 1;
    lpDown |= static_cast<LPARAM>(::MapVirtualKeyW(key, MAPVK_VK_TO_VSC)) << 16;
    LPARAM lpUp = lpDown | static_cast<LPARAM>(0xC0000000U);
    hooks::PostInjectedInput(hwnd, WM_KEYDOWN, key, lpDown);
    std::this_thread::sleep_for(SETTLE_DELAY);
    hooks::PostInjectedInput(hwnd, WM_KEYUP, key, lpUp);
    // See SendClick -- trailing pause keeps tight script loops from racing the
    // game's input pump.
    std::this_thread::sleep_for(SETTLE_DELAY);
}

// === Pathfinding ===

// Walk the player's drlg chain looking for a level whose subtile bounds cover
// `gamePos`. Game-coords are subtile x 5, so we divide before comparing -- same
// observable result as reference's per-level rectangle check.
Level FindLevelAt(Position gamePos) {
    auto guard = Bridge::Lock();
    auto* player = d2client::UNITS_GetPlayerUnit();
    // `D2UnitStrc::pDrlgAct` is typed as D2MOO's `::D2DrlgActStrc*`; the bytes follow
    // the 1.14d allocation modeled by `extras::D2DrlgActStrc`.
    auto* act = (player != nullptr) ? reinterpret_cast<D2DrlgActStrc*>(player->pDrlgAct) : nullptr;
    if (act == nullptr || act->pDrlg == nullptr) {
        return Level();
    }
    constexpr uint32_t SUBTILE_SCALE = 5;
    const Position subtile{.x = gamePos.x / SUBTILE_SCALE, .y = gamePos.y / SUBTILE_SCALE};
    for (auto* level = act->pDrlg->pLevel; level != nullptr; level = level->pNextLevel) {
        // nPosX/Y/Width/Height are int32 in the struct; non-negative in practice
        // for every valid level. Clamp to 0 defensively before promoting to the
        // unsigned Rect/Position framework primitives.
        const auto toU = [](int32_t v) {
            return static_cast<uint32_t>(std::max(v, 0));
        };
        const Rect bounds{.origin = {.x = toU(level->nPosX), .y = toU(level->nPosY)},
                          .size = {.width = toU(level->nWidth), .height = toU(level->nHeight)}};
        if (bounds.Contains(subtile)) {
            return Level(static_cast<uint32_t>(level->nLevelId));
        }
    }
    return Level();
}

// === MPQ ===

void LoadMpq(const std::string& path) {
    if (path.empty()) {
        return;
    }
    // Reference Core.cpp:181-184 LoadMPQ. Priority 3000 places the MPQ at the
    // top of the search chain so its overrides win against the core MPQs.
    // Resetting the BNet keys forces them to be re-derived on the next login
    // against the new MPQ chain (stale keys would mismatch the new content).
    d2win::ARCHIVE_InitMPQ(path.c_str(), nullptr, 0, 3000);
    *bnclient::gpszExpansionCdKey = nullptr;
    *bnclient::gpszClassicCdKey = nullptr;
    *bnclient::gpszKeyOwner = nullptr;
}

// === Launch ===

std::optional<std::string> GetLaunchProfile() {
    return GetLaunchOptions().profile;
}

}  // namespace d2bs::game
