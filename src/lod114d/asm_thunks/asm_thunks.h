#pragma once

#include <cstdint>

struct D2AutomapLayerStrc;
struct D2InventoryGridInfoStrc;
struct D2InventoryStrc;
struct D2RosterUnitStrc;
struct D2UnitStrc;

// Naked-asm thunks for game-side function entries whose register
// conventions cannot be expressed in C. Each thunk loads its arguments
// into the registers the underlying function expects, then jumps to the
// resolved game address.
//
// The functions wrapped here all live in 1.14d Game.exe; their register
// shapes are inherited directly from the 1.13d-era reference d2bs.
//
// Detours is not used for these - Detours installs function-replacement
// hooks, not register-shuffle trampolines. A naked thunk is the smallest
// expression of "set ecx/edx/eax/edi to these values, then jmp".

namespace d2bs::asm_thunks {

// Resolve every thunk's target address from the imports::d2client
// GameAsmFunc instances. Must be called after Bridge::Init() has run
// the registry's ResolveAll step. Returns false (after popping a
// MessageBoxW naming the unresolved entry) if any required asm-thunk
// import is still zero.
[[nodiscard]] bool Init();

// TakeWaypoint_I @ 0x9D0F1, mid-function in sub_49D010.
// Reference: D2Helpers.cpp:872 (`D2CLIENT_TakeWaypoint`).
void __stdcall TakeWaypoint(uint32_t waypointId, uint32_t area);

// ClickBelt_I @ 0x98870, function entry, __userpurge(eax, stack).
// Reference: D2Helpers.cpp:783 (`D2CLIENT_ClickBelt`).
void __fastcall ClickBelt(int32_t x, int32_t y, D2InventoryStrc* pInventoryData);

// ClickBeltRight_I @ 0x98A90, function entry; reads eax, ecx, edx.
// Reference: D2Helpers.cpp:810 (`D2CLIENT_ClickBeltRight_ASM`).
void __fastcall ClickBeltRight(D2InventoryStrc* pInventory, D2UnitStrc* pPlayer, bool holdShift, uint32_t potPos);

// ClickItemRight_I @ 0x87740, function entry; reads eax, ecx, edx + stack.
// Reference: D2Helpers.cpp:800 (`D2CLIENT_ClickItemRight_ASM`).
void __fastcall ClickItemRight(int32_t x, int32_t y, uint32_t location, D2UnitStrc* pPlayer,
                               D2InventoryStrc* pInventory);

// ClickParty_II @ 0x9B990, function entry; reads eax + ecx.
// Reference: D2Helpers.cpp:947 (`D2CLIENT_HostilePartyUnit`).
void __fastcall HostilePartyUnit(D2RosterUnitStrc* pRosterUnit, uint32_t button);

// Say_I @ 0x7CBDA, mid-function in sub_47CA70 (OOG event handler).
// Reference: Core.cpp:67 (`Say_ASM`). Caller is responsible for first
// copying the wide message to *imports::d2client::gwszChatMsg.
void __fastcall Say(const void* msgPtr);

// SetSelectedUnit_I @ 0x66DE0 - `__usercall sub@<eax>(arg@<eax>)`. Function
// reads its argument from EAX. Reference: D2Helpers.cpp:728
// (`D2CLIENT_SetSelectedUnit_STUB`).
void __fastcall SetSelectedUnit(D2UnitStrc* pUnit);

// LeftClickItem_I @ 0x8FFE0 - `__userpurge sub@<eax>(a1@<eax>, ...)`. Function
// reads Location from EAX and the remaining 6 args from the stack. Reference:
// D2Helpers.cpp:790 (`D2CLIENT_LeftClickItem`).
void __stdcall LeftClickItem(uint32_t location, D2UnitStrc* pPlayer, D2InventoryStrc* pInventory, int32_t x, int32_t y,
                             uint32_t dwClickType, D2InventoryGridInfoStrc* pLayout);

// InitAutomapLayer_I @ 0x58D40 - `__usercall sub@<eax>(a1@<edi>)`. Function
// expects nLayerNo in EDI and is callee-saved. Reference: D2Helpers.cpp:336
// (`D2CLIENT_InitAutomapLayer_STUB`).
D2AutomapLayerStrc* __fastcall InitAutomapLayer(uint32_t nLayerNo);

// Translate a level number into its automap layer number via D2COMMON_GetLayer
// (AutomapLayer2 has nLayerNo at offset +0x08), then invoke InitAutomapLayer
// with the resolved layer number. Reference: D2Helpers.cpp:347-350.
D2AutomapLayerStrc* InitAutomapLayerForLevel(uint32_t levelNo);

}  // namespace d2bs::asm_thunks
