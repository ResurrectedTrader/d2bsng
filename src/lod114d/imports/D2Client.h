#pragma once

#include "D2MOOConfig.h"
#include "ImportTypes.h"
#include "game/Types.h"

#include "extras/D2ActiveRoomStrc.h"
#include "extras/D2UnitHashTables.h"
#include "extras/GameStructInfo.h"
#include "extras/NPCMenu.h"
#include "extras/TransactionDialogs.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
// D2Roster.h forward-references D2CellFileStrc without including it; same for
// D2Structs.OtherDLLs.h's D2MenuItemStrc / D2GfxDataStrc, and SMSGHANDLER_PARAMS.
struct D2CellFileStrc;
struct D2MenuItemStrc;
struct D2GfxDataStrc;
// NOLINTNEXTLINE(readability-identifier-naming) - matches D2MOO upstream type name
struct SMSGHANDLER_PARAMS;
#include <D2BitManip.h>           // D2BitBufferStrc
#include <D2Constants.h>          // D2C_Difficulties
#include <D2Inventory.h>          // D2InventoryStrc
#include <D2Roster.h>             // D2RosterUnitStrc
#include <D2Structs.OtherDLLs.h>  // D2AutomapCellStrc, D2AutomapLayerStrc
#include <D2Waypoints.h>          // D2WaypointDataStrc
#include <DataTbls/InvTbls.h>     // D2InventoryGridInfoStrc
#include <Units/Units.h>          // D2UnitStrc
#pragma clang diagnostic pop

#include <array>
#include <cstdint>

// All offsets below are Game.exe-relative for 1.14d. Calling conventions are
// sourced from reference/d2bs/D2Ptrs.h (FUNCPTR / VARPTR macro tags); D2MOO's
// own declarations are not consulted for conventions per GAME_IMPL.md sec 3.0.
//
// Per-DLL grouping mirrors reference/d2bs/D2Ptrs.h's DLL tag; runtime is
// monolithic, so it has no functional effect, only categorisation.

// NOLINTBEGIN(readability-identifier-naming) - MOO-style names use DOMAIN_PascalCase with embedded underscores
namespace d2bs::imports::d2client {

// 1.14d-correct shadow types live in `imports::extras`. Aliasing them inside
// this namespace lets call-sites use `d2client::RevealAutomapRoom` etc. with
// the correct 1.14d layouts without further qualification.
using extras::D2ActiveRoomStrc;

// ---- Functions -------------------------------------------------------------
inline StdcallFunc<D2BitBufferStrc*()> QUESTRECORD_GetQuestInfo{0xB32D0};
inline FastcallFunc<void(uint32_t /*dwItemId*/)> ITEMS_Submit{0xB2370};
inline FastcallFunc<void()> ITEMS_Transmute{0x8A0D0};

inline FastcallFunc<D2UnitStrc*(uint32_t /*dwId*/, game::UnitType /*dwType*/)> UNITS_GetClientSideUnit{0x63990};
inline FastcallFunc<D2UnitStrc*(uint32_t /*dwId*/, game::UnitType /*dwType*/)> UNITS_GetServerSideUnit{0x639B0};
inline FastcallFunc<D2UnitStrc*()> UI_GetInteractingNPC{0xB1620};
inline StdcallFunc<D2UnitStrc*()> UNITS_GetSelectedUnit{0x67A10};
// `size_t` matches `DWORD` on Win32 (both 32-bit) and lets callers pass
// `std::array::size()` directly without `static_cast<uint32_t>`.
inline FastcallFunc<int32_t(D2UnitStrc* /*pItem*/, wchar_t* /*wBuffer*/, size_t /*nSize*/)> ITEMS_GetName{0x8C060};
inline StdcallFunc<int32_t(D2UnitStrc* /*pItem*/, int32_t /*type*/)> ITEMS_LoadDescription{0x8DD90};
inline FastcallFunc<uint32_t(uint32_t /*nMonsterId*/)> MONSTERS_GetOwner{0x79150};
inline FastcallFunc<void()> INVENTORY_Init{0x845A0};
// Third arg's role isn't reconstructed: no MOO equivalent, and every reference
// d2bs / d2bs-ascii caller passes 0 for it.
inline FastcallFunc<uint32_t(uint32_t /*varno*/, uint32_t /*howset*/, uint32_t /*dwUnknown*/)> UI_SetVar{0x55F20};

// Args 4, 7, 8 unreconstructed: no MOO equivalent (D2Client source is not in
// MOO), and reference d2bs's only annotations are `unk` / `_2` / `_3`. Every
// reference and d2bs-ascii caller passes 0/1/0 respectively.
inline FastcallFunc<void(D2UnitStrc* /*pNpc*/, D2UnitStrc* /*pItem*/, uint32_t /*dwSell*/, uint32_t /*dwUnknown1*/,
                         uint32_t /*dwItemCost*/, uint32_t /*dwMode*/, uint32_t /*dwUnknown2*/,
                         uint32_t /*dwUnknown3*/)>
    NPCS_ShopAction{0xB3870};

inline FastcallFunc<void()> UI_CloseNPCInteract{0xB3F10};
inline FastcallFunc<void()> UI_CloseInteract{0x4C6B0};

inline StdcallFunc<uint32_t()> AUTOMAP_GetSize{0x5A710};
inline FastcallFunc<D2AutomapCellStrc*()> AUTOMAP_NewCell{0x57C30};
inline FastcallFunc<void(D2AutomapCellStrc* /*aCell*/, D2AutomapCellStrc** /*node*/)> AUTOMAP_AddCell{0x57B00};
inline StdcallFunc<void(D2ActiveRoomStrc* /*pRoom1*/, uint32_t /*dwClipFlag*/, D2AutomapLayerStrc* /*aLayer*/)>
    AUTOMAP_RevealRoom{0x58F40};

inline FastcallFunc<void(uint32_t /*MouseFlag*/, uint32_t /*x*/, uint32_t /*y*/, uint32_t /*Type*/)> UI_ClickMap{
    0x62D00};

inline FastcallFunc<uint32_t()> UI_GetMouseXOffset{0x5AFC0};
inline FastcallFunc<uint32_t()> UI_GetMouseYOffset{0x5AFB0};

inline FastcallFunc<void(const wchar_t* /*wMessage*/, int32_t /*nColor*/)> UI_PrintGameString{0x9E3A0};

inline FastcallFunc<void()> PARTY_Leave{0x79FC0};

inline FastcallFunc<void()> TRADE_Accept{0xB9070};
inline FastcallFunc<void()> TRADE_Cancel{0xB90B0};
inline StdcallFunc<void()> TRADE_OK{0xB8A30};

// 1.14d returns the difficulty as a BYTE in AL (reference D2Ptrs.h: GetDifficulty BYTE),
// so the import reads uint8_t; a wider return type reads the uninitialised high bytes of
// EAX. game::GetDifficulty() converts the byte to our game::Difficulty.
inline StdcallFunc<uint8_t()> GAME_GetDifficulty{0x4DCD0};
inline FastcallFunc<void()> GAME_Exit{0x4DD60};
inline FastcallFunc<uint32_t(uint32_t /*dwVarNo*/)> UI_GetVar{0x538D0};

// Takes the address of a contiguous int32 quad (left, top, right, bottom).
// Reference FUNCPTR types it as `DWORD Rect` (the address as integer); reference
// D2Helpers.cpp:919 wraps it as `RECT* rect`. We use the typed pointer here so
// callers don't need a `reinterpret_cast<uint32_t>` at the call site.
inline FastcallFunc<void(const int32_t* /*pRect*/)> UI_DrawRectFrame{0x52E50};
inline FastcallFunc<void()> UI_PerformGoldDialogAction{0x54080};

inline StdcallFunc<D2UnitStrc*()> UNITS_GetPlayerUnit{0x63DD0};

inline FastcallFunc<void()> UI_ClearScreen{0xB4620};

// Address-compared (never called) to detect that an NPC dialog is scrolling -
// d2bs checks whether Storm registered this fn as the 0x201 (WM_LBUTTONDOWN)
// handler for the game window. The arg is the message-envelope pointer that
// Storm's WndProc dispatcher passes to handlers.
inline StdcallFunc<uint32_t(void* /*pMsg*/)> UI_CloseNPCTalk{0xA17D0};

inline FastcallFunc<uint32_t(uint32_t /*dwUnitId1*/, uint32_t /*dwUnitId2*/, uint32_t /*dwFlag*/)> PLAYERLIST_CheckFlag{
    0xDC440};

inline FastcallFunc<uint32_t()> GAME_GetLanguageCode{0x125150};

inline FastcallFunc<void(uint32_t /*bPacketType*/, uint32_t /*dwSlot*/)> MERCENARY_ItemAction{0x785B0};
inline FastcallFunc<wchar_t*(D2UnitStrc* /*pUnit*/)> UNITS_GetName{0x64A60};
inline FastcallFunc<uint32_t(D2UnitStrc* /*pUnit*/, uint32_t /*dwType*/)> UNITS_GetMinionCount{0x78EE0};

// Plays sound `soundId`, sourced at `pUnit` (the active player in practice);
// when `pUnit` is the player unit the game raises the sound's priority. Args
// 3-5 are written to the sound record but their roles are unreconstructed -
// callers pass 0. No reference D2Ptrs.h entry exists (reference's
// `D2CLIENT_PlaySound` is a commented-out stub); offset read from the 1.14d
// binary.
inline FastcallFunc<int32_t(uint32_t /*soundId*/, D2UnitStrc* /*pUnit*/, int32_t, int32_t, int32_t)> SOUND_PlaySound{
    0xB9A00};

// ---- Variables -------------------------------------------------------------
inline GameVar<game::Size> gScreenSize{0x31146C};  // width @ +0, height @ +4

// -1 means no cell hovered.
inline GameVar<game::Point> gCursorHover{0x321E4C};  // x @ +0, y @ +4

// Mouse position in client coordinates. The game stores Y at the lower offset
// (+0) and X at the higher (+4), opposite to `Point`/`Position`'s {x, y} layout.
// Keep these as separate vars to avoid a swap-on-deref trap; collapsing into a
// {x, y} struct would silently read the wrong axis.
inline GameVar<uint32_t> gnMouseY{0x3A6AAC};
inline GameVar<uint32_t> gnMouseX{0x3A6AB0};

// Mouse-offset (viewport scroll) in client coords. Same Y@+0, X@+4 layout as
// `gnMouseY`/`gnMouseX`; see the note above for why these are not collapsed.
inline GameVar<int32_t> gnMouseOffsetY{0x3A5208};
inline GameVar<int32_t> gnMouseOffsetX{0x3A520C};

inline GameVar<uint32_t> gbAutomapOn{0x3A27E8};
// `gnAutomapMode` is the divisor used by both ScreenToAutomap and AutomapToScreen
// transforms; the address aliases reference's commonly-named "Divisor" VARPTR.
inline GameVar<int32_t> gnAutomapMode{0x311254};
inline GameVar<game::Point> gAutomapOffset{0x3A5198};  // x @ +0, y @ +4
inline GameVar<D2AutomapLayerStrc*> gpAutomapLayer{0x3A5164};

inline GameVar<uint32_t> gnMercReviveCost{0x3C0DD0};

// 6 type-tables * 128 hash buckets each. Server-side covers 5 types
// (Player/Monster/Object/Item/Tile); missiles live in the client-side
// table at type-slot 3. See imports/extras/D2UnitHashTables.h.
inline GameVar<extras::D2UnitHashTables> gServerSideUnitHashTables{0x3A5E70};
inline GameVar<extras::D2UnitHashTables> gClientSideUnitHashTables{0x3A5270};

// Signed: framework `GoldAction(GoldActionMode, int32_t amount)` takes int32
// amount. The IDA decompile types this as signed; the game only ever writes
// non-negative values so the bit pattern matches reference's DWORD VARPTR.
inline GameVar<int32_t> gnGoldDialogAction{0x3A279C};
inline GameVar<int32_t> gnGoldDialogAmount{0x3A2A68};

inline GameVar<extras::NPCMenu> gNPCMenu{0x326C48};
inline GameVar<uint32_t> gnNPCMenuAmount{0x325A74};

// Container layout structs (TradeLayout/StashLayout/CubeLayout/InventoryLayout/
// StoreLayout/MercLayout) are intentionally not declared here. The game lazily
// populates their cell-size bytes on first panel open, and a raw call into a
// click thunk with an uninitialised layout divides by zero. Use
// game::ResolveContainerLayout() in GameHelpers; it owns the GameVar
// instances and force-runs INVENTORY_Init if the layout hasn't been touched.

inline GameVar<uint32_t> gnRegularCursorType{0x3A6AF0};
inline GameVar<uint32_t> gnShopCursorType{0x3BCBF0};

inline GameVar<uint32_t> gnPing{0x3A04A4};
inline GameVar<uint32_t> gnFPS{0x3BB390};
inline GameVar<uint32_t> gnLang{0x3BB5DC};

inline GameVar<uint32_t> gnOverheadTrigger{0x3BF20E};

inline GameVar<uint32_t> gnRecentInteractId{0x3C0D25};
// Pointer to the player's quest-flag bit buffer. Passed as `pQuestFlags` to
// ITEMS_GetTransactionCost / ITEMS_GetAllRepairCosts (vendor pricing varies
// with quest progression). Reference VARPTRs it as DWORD; we type it as
// `D2BitBufferStrc*` so callers don't reinterpret_cast at the call site.
inline GameVar<D2BitBufferStrc*> gpItemPriceList{0x3C0D43};

inline GameVar<void*> gpTransactionDialog{0x3C0D63};
inline GameVar<uint32_t> gnTransactionDialogs{0x3C0E5C};
inline GameVar<uint32_t> gnTransactionDialogs_2{0x3C0E58};
inline GameVar<extras::TransactionDialogsInfo*> gpTransactionDialogsInfo{0x3C0E54};

inline GameVar<extras::GameStructInfo*> gpGameInfo{0x3A0438};

// Pointer to the player's waypoint-bit-buffer. Passed as `pData` to
// WAYPOINTS_IsActivated. Reference VARPTRs it as DWORD; we type it as
// `D2WaypointDataStrc*` so callers don't reinterpret_cast at the call site.
inline GameVar<D2WaypointDataStrc*> gpWaypointTable{0x3BF081};

inline GameVar<D2UnitStrc*> gpPlayerUnit{0x3A6A70};
inline GameVar<D2UnitStrc*> gpSelectedInvItem{0x3BCBF4};
inline GameVar<D2RosterUnitStrc*> gpPlayerUnitList{0x3BB5C0};

inline GameVar<uint32_t> gnWeaponSwitch{0x3BCC4C};

inline GameVar<uint32_t> gbTradeAccepted{0x3BCE18};
inline GameVar<uint32_t> gbTradeBlock{0x3BCE28};

// Signed: `int32_t GetRecentTradeId()` is the framework return type; bit
// pattern matches reference's DWORD VARPTR for the range of valid state ids
// (0..7).
inline GameVar<int32_t> gnRecentTradeId{0x3C0E7C};

inline GameVar<uint32_t> gbExpCharFlag{0x3A04F4};
inline GameVar<uint32_t> gnMapId{0x3A0638};

inline GameVar<uint32_t> gbAlwaysRun{0x3A0660};
inline GameVar<uint32_t> gbNoPickUp{0x3A6A90};

inline GameVar<std::array<wchar_t, 257>> gwszChatMsg{0x3BB638};
inline GameVar<uint32_t> gnOrificeId{0x3C547C};
inline GameVar<uint32_t> gnCursorItemMode{0x3C5474};

// Item-description redirect. Setting `gbItemDescFlag` to non-zero and
// `gpItemDescItem` to the target item makes `ITEMS_LoadDescription` follow the
// "build description for this specific item, not the cursor item" path, which
// ultimately calls sub_502280 (the screen-text store function that copies its
// wstring arg to `gwszItemDescBuffer`). Reading the buffer afterwards gives
// the rendered description. Reference uses WriteProcessMemory + reads from
// D2Win-relative `0x441EC8`; we're in-process so direct memory access works.
inline GameVar<uint32_t> gbItemDescFlag{0x3BCBE8};
inline GameVar<D2UnitStrc*> gpItemDescItem{0x3BCBF4};
inline GameVar<std::array<wchar_t, 1024>> gwszItemDescBuffer{0x441EC8};

// ---- ASM-thunk targets -----------------------------------------------------
// Each address is wrapped by a __declspec(naked) thunk in
// src/lod114d/asm_thunks/ that supplies the non-standard register input the
// underlying function expects, then jmps to the resolved address. The
// GameAsmFunc instances here exist purely to register the offsets with the
// import registry; the asm-thunk module reads .Addr() during its Init().
inline GameAsmFunc TakeWaypoint_I{0x9D0F1};
inline GameAsmFunc ClickBelt_I{0x98870};
inline GameAsmFunc ClickBeltRight_I{0x98A90};
inline GameAsmFunc ClickItemRight_I{0x87740};
inline GameAsmFunc ClickParty_II{0x9B990};
inline GameAsmFunc Say_I{0x7CBDA};
inline GameAsmFunc SetSelectedUnit_I{0x66DE0};   // __usercall(arg@<eax>)
inline GameAsmFunc LeftClickItem_I{0x8FFE0};     // __userpurge(a1@<eax>, ...)
inline GameAsmFunc InitAutomapLayer_I{0x58D40};  // __usercall(a1@<edi>)

// ---- Inline-patch intercept tail-call targets ------------------------------
// Hook subsystem (`src/lod114d/hooks/Intercepts.cpp`) overwrites a CALL/JMP at a
// game-side patch site with a JMP into our `__declspec(naked)` thunk. The
// thunk does its `pushad` + C dispatcher, then either bails (block) or
// tail-jumps to the original target; these addresses are those targets.
inline GameAsmFunc InputCall_I{0x787B0};       // CALL site at 0x47C89D
inline GameAsmFunc CongratsScreen_I{0xF6190};  // CALL site at 0x44EBEF
inline GameAsmFunc SendPacket_II{0x12AE62};    // tail target for the SendPacket fall-through

// Directly callable, no thunk: IDA decompile is `__thiscall(this)` (single
// arg). Reference's 2-arg signature included an unused `type` arg dropped here.
inline FastcallFunc<void(D2RosterUnitStrc* /*pRosterUnit*/)> ClickParty_I{0x79EB0};

// Data table: function-pointer array per body location. Indexed at the call
// site; only the first 11 slots are valid in actual game memory.
using BodyClickFn = void(__fastcall*)(D2UnitStrc* /*pPlayer*/, D2InventoryStrc* /*pInventory*/, int32_t /*nBodyLoc*/);
inline GameVar<std::array<BodyClickFn, 11>> gaBodyClickTable{0x321E58};

// Not declared in this layer:
//   AssignPlayer_I, GameLeave_I, GameAddUnit_I: intercept-only entries the
//     hook subsystem owns; reference's intercept installation does not
//     currently install them.
//   ClickShopItem_I, ShopAction_I, GetItemDesc_I, LoadUIImage_I, Interact_I:
//     reference's only call site is commented out
//     (`reference/d2bs/JSUnit.cpp:882`).
//   SendGamePacket_I: reference's wrapper around D2NET_SendPacket adds same-
//     packet debounce (50/200ms) + length cap. Our port calls D2NET_SendPacket
//     directly; the debounce is moot because every caller spaces sends with
//     Sleep(500). Functionally identical for our use cases.

}  // namespace d2bs::imports::d2client
// NOLINTEND(readability-identifier-naming)
