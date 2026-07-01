#include "asm_thunks/asm_thunks.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "imports/D2Client.h"
#include "imports/D2Common.h"

// Each thunk does an indirect `jmp dword ptr [g_X]` to the resolved
// Game.exe address. The g_* globals are populated by Init() after the
// imports::Registry has run ResolveAll, so before Init() runs every
// thunk would jump to address 0. Init() is invoked from Bridge::Init.
//
// The asm shapes match reference/d2bs/D2Helpers.cpp and
// reference/d2bs/Core.cpp (Say_ASM).
//
// Two reasons Detours can't replace these (it installs function-entry
// hooks, not register-shuffle trampolines - see the header):
//   1. Mid-function entries (TakeWaypoint, Say) jump into the middle of
//      sub_49D010 / sub_47CA70 with a hand-crafted stack frame. There is
//      no prologue at those bytes for Detours to patch.
//   2. Register-passed args (ClickBelt*, ClickItemRight, HostilePartyUnit,
//      SetSelectedUnit, LeftClickItem, InitAutomapLayer) need callers to
//      load eax / ecx / edx / edi before the call. Detours preserves
//      whatever calling convention the target already has - it cannot
//      shuffle args between registers and stack on behalf of the caller.
//
// Every thunk declared in the header has at least one call site in the
// d2bs DLL (see asm_thunks::* references in src/backends/lod114d/game/). Unreferenced
// entries are not maintained.

namespace d2bs::asm_thunks {

namespace {

// 1.14d D2COMMON_GetLayer (@ 0x21E470) returns a pointer to an
// AutomapLayer2 record (reference d2bs D2Structs.h:106). D2MOO models
// the standard D2AutomapLayerStrc whose nLayerNo is at offset 0x00 -
// AutomapLayer2 is a separate, smaller structure that is not in D2MOO.
// The only field we need is nLayerNo at offset 0x08, so the struct is
// declared here at the single point of use rather than synthesised as
// a shared shadow type.
// NOLINTBEGIN(readability-identifier-naming) - opaque padding fields match imports/extras/ shadow-struct convention
struct AutomapLayer2 {
    uint32_t _0;        // 0x00 - opaque
    uint32_t _4;        // 0x04 - opaque
    uint32_t nLayerNo;  // 0x08
};
// NOLINTEND(readability-identifier-naming)
static_assert(offsetof(AutomapLayer2, nLayerNo) == 0x08, "AutomapLayer2::nLayerNo offset drift");

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - asm thunks need module-level pointers
uintptr_t takeWaypoint = 0;
uintptr_t clickBelt = 0;
uintptr_t clickBeltRight = 0;
uintptr_t clickItemRight = 0;
uintptr_t hostilePartyUnit = 0;
uintptr_t sayAddr = 0;
uintptr_t setSelectedUnit = 0;
uintptr_t leftClickItem = 0;
uintptr_t initAutomapLayer = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

bool Init() {
    // Snapshot every thunk's target address, failing hard with a message box
    // if any import is still unresolved (ResolveAll not yet run) instead of
    // letting a thunk tail-jump to a near-zero address.
    struct Entry {
        const char* name;
        const imports::GameAsmFunc& fn;
        uintptr_t* slot;
    };
    const std::array<Entry, 9> entries = {{
        {.name = "D2CLIENT TakeWaypoint_I", .fn = imports::d2client::TakeWaypoint_I, .slot = &takeWaypoint},
        {.name = "D2CLIENT ClickBelt_I", .fn = imports::d2client::ClickBelt_I, .slot = &clickBelt},
        {.name = "D2CLIENT ClickBeltRight_I", .fn = imports::d2client::ClickBeltRight_I, .slot = &clickBeltRight},
        {.name = "D2CLIENT ClickItemRight_I", .fn = imports::d2client::ClickItemRight_I, .slot = &clickItemRight},
        {.name = "D2CLIENT ClickParty_II", .fn = imports::d2client::ClickParty_II, .slot = &hostilePartyUnit},
        {.name = "D2CLIENT Say_I", .fn = imports::d2client::Say_I, .slot = &sayAddr},
        {.name = "D2CLIENT SetSelectedUnit_I", .fn = imports::d2client::SetSelectedUnit_I, .slot = &setSelectedUnit},
        {.name = "D2CLIENT LeftClickItem_I", .fn = imports::d2client::LeftClickItem_I, .slot = &leftClickItem},
        {.name = "D2CLIENT InitAutomapLayer_I", .fn = imports::d2client::InitAutomapLayer_I, .slot = &initAutomapLayer},
    }};

    for (const auto& e : entries) {
        if (!e.fn.IsResolved()) {
            std::wstring msg = L"d2bsng: failed to initialise asm thunks - unresolved import: ";
            for (const char* p = e.name; *p != '\0'; ++p) {
                msg.push_back(static_cast<unsigned char>(*p));
            }
            MessageBoxW(nullptr, msg.c_str(), L"d2bsng init failure", MB_OK | MB_ICONERROR);
            return false;
        }
        *e.slot = e.fn.Addr();
    }
    return true;
}

// Reference D2Helpers.cpp:872. Two-level frame: the outer thunk builds a
// 0x20-byte struct on the stack, then an inner trampoline loads the
// registers the mid-function entry expects (edi=0, ebx=1, edx=wpId,
// ecx=area, esi=struct) and jmps in.
__declspec(naked) void __stdcall TakeWaypoint(uint32_t /*waypointId*/, uint32_t /*area*/) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x20
        push ebx
        push esi
        push edi
        and dword ptr ss:[ebp-0x20], 0
        push 0
        call inner_take_waypoint
        pop edi
        pop esi
        pop ebx
        leave
        ret 8

    inner_take_waypoint:
        push ebp
        push ebx
        push esi
        push edi
        xor edi, edi
        mov ebx, 1
        mov edx, dword ptr ss:[ebp + 8]  // waypointId
        mov ecx, dword ptr ss:[ebp + 0xC]  // area
        push ecx
        lea esi, dword ptr ss:[ebp - 0x20]
        jmp dword ptr [takeWaypoint]
    }
}

// Reference D2Helpers.cpp:783 - `D2CLIENT_ClickBelt`.
__declspec(naked) void __fastcall ClickBelt(int32_t /*x*/, int32_t /*y*/, D2InventoryStrc* /*pInventoryData*/) {
    __asm {
        mov eax, edx
        jmp dword ptr [clickBelt]
    }
}

// Reference D2Helpers.cpp:810 - `D2CLIENT_ClickBeltRight_ASM`.
__declspec(naked) void __fastcall ClickBeltRight(D2InventoryStrc* /*pInventory*/, D2UnitStrc* /*pPlayer*/,
                                                 bool /*holdShift*/, uint32_t /*potPos*/) {
    __asm {
        pop eax  // return address
        xchg eax, [esp]  // ret addr -> stack, holdShift -> eax
        jmp dword ptr [clickBeltRight]
    }
}

// Reference D2Helpers.cpp:800 - `D2CLIENT_ClickItemRight_ASM`.
// Caller passes 5 fastcall args: ecx=x, edx=y, plus location/player/inventory
// on the stack. Function expects ecx=y, edx=x, eax=location, stack=player+inventory.
__declspec(naked) void __fastcall ClickItemRight(int32_t /*x*/, int32_t /*y*/, uint32_t /*location*/,
                                                 D2UnitStrc* /*pPlayer*/, D2InventoryStrc* /*pInventory*/) {
    __asm {
        xchg edx, ecx  // x <-> y
        pop eax  // return address
        xchg eax, [esp]  // ret addr -> stack, location -> eax
        jmp dword ptr [clickItemRight]
    }
}

// Reference D2Helpers.cpp:947 - `D2CLIENT_HostilePartyUnit`.
__declspec(naked) void __fastcall HostilePartyUnit(D2RosterUnitStrc* /*pRosterUnit*/, uint32_t /*button*/) {
    __asm {
        mov eax, edx
        jmp dword ptr [hostilePartyUnit]
    }
}

// Reference Core.cpp:67 - `Say_ASM`. Replicates sub_47CA70's prologue
// (0x110-byte stack frame) so the mid-function entry at Say_I lands in
// a frame shape it expects. Caller must have populated *ChatMsg with
// the message text before calling.
__declspec(naked) void __fastcall Say(const void* /*msgPtr*/) {
    __asm {
        pop eax  // return address
        push ecx  // msg ptr -> first stack arg
        push eax  // return address back on top
        push ebp
        mov ebp, esp
        sub esp, 0x110
        push ebx
        push esi
        push edi
        mov ebx, dword ptr [ebp + 8]  // ebx = msg ptr (pushed earlier)
        jmp dword ptr [sayAddr]
    }
}

// Reference D2Helpers.cpp:728 - `D2CLIENT_SetSelectedUnit_STUB`. Caller
// fastcalls (ecx=pUnit); function reads the argument from EAX. Move ECX
// into EAX and tail-jump.
__declspec(naked) void __fastcall SetSelectedUnit(D2UnitStrc* /*pUnit*/) {
    __asm {
        mov eax, ecx
        jmp dword ptr [setSelectedUnit]
    }
}

// Reference D2Helpers.cpp:790 - `D2CLIENT_LeftClickItem`. Caller stdcalls
// (Location, pPlayer, pInventory, x, y, dwClickType, pLayout); function
// expects Location in EAX and the remaining 6 args on the stack. Pop the
// return address, exchange it with the first stack arg (Location -> EAX,
// ret addr -> top of stack), then tail-jump.
__declspec(naked) void __stdcall LeftClickItem(uint32_t /*location*/, D2UnitStrc* /*pPlayer*/,
                                               D2InventoryStrc* /*pInventory*/, int32_t /*x*/, int32_t /*y*/,
                                               uint32_t /*dwClickType*/, D2InventoryGridInfoStrc* /*pLayout*/) {
    __asm {
        pop eax  // return address
        xchg eax, [esp]  // ret addr -> stack, Location -> eax
        jmp dword ptr [leftClickItem]
    }
}

// Reference D2Helpers.cpp:336 - `D2CLIENT_InitAutomapLayer_STUB`. Caller
// fastcalls (ecx=nLayerNo); function expects nLayerNo in EDI. Preserve
// EDI (callee-saved), move ECX into EDI, call the function, restore EDI.
__declspec(naked) D2AutomapLayerStrc* __fastcall InitAutomapLayer(uint32_t /*nLayerNo*/) {
    __asm {
        push edi
        mov edi, ecx
        call dword ptr [initAutomapLayer]
        pop edi
        ret
    }
}

D2AutomapLayerStrc* InitAutomapLayerForLevel(uint32_t levelNo) {
    const auto* layerInfo = static_cast<const AutomapLayer2*>(imports::d2common::DRLG_GetLayer(levelNo));
    const uint32_t layerNo = (layerInfo != nullptr) ? layerInfo->nLayerNo : 0;
    return InitAutomapLayer(layerNo);
}

}  // namespace d2bs::asm_thunks
