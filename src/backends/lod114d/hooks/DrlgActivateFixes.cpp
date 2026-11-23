#include "hooks/DrlgActivateFixes.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include "detour/Hook.h"
#include "imports/D2Common.h"
#include "imports/ImportTypes.h"
#include "utils/utils.h"

namespace d2bs::lod114d::hooks::drlg_activate {
namespace {
// DRLGACTIVATE_Update resumes its walk of the out-of-sight list at D2DrlgStrc::pDrlgRoom, which
// nothing clears when DRLG_UpdateAndFreeInactiveRooms frees that room's level. The game's only
// check reads the freed room's status byte. The client and the single-player server allocate
// from one pool, so the block can by then be a server room, or any object whose byte at that
// offset is ROOMSTATUS_CLIENT_OUT_OF_SIGHT; the walk then leaves the list and faults. A cursor
// that is not on the list is cleared, which the game treats as a restart from the list head,
// as it does when the room's status has changed.
bool IsOnOutOfSightList(const D2DrlgStrc* drlg, const D2DrlgRoomStrc* room) {
    const auto* head = &drlg->tStatusRoomsLists[ROOMSTATUS_CLIENT_OUT_OF_SIGHT];
    if (room == head) {
        return true;
    }
    for (const auto* entry = head->pStatusNext; entry != nullptr && entry != head; entry = entry->pStatusNext) {
        if (entry == room) {
            return true;
        }
    }
    return false;
}

void __stdcall UpdateHook(D2DrlgStrc* drlg);
core::detour::Hook<decltype(&UpdateHook)> updateHook{&UpdateHook};
bool installed = false;

void __stdcall UpdateHook(D2DrlgStrc* drlg) {
    // The game reads the cursor only on the call that takes the countdown from 1 to 0.
    if (drlg->pDrlgRoom != nullptr && drlg->nRoomsInitSinceLastUpdate <= 1 && drlg->nRoomsInitTimeout == 1 &&
        !IsOnOutOfSightList(drlg, drlg->pDrlgRoom)) {
        drlg->pDrlgRoom = nullptr;
    }
    updateHook(drlg);
}
}  // namespace

void Install() {
    if (installed) {
        return;
    }
    constexpr uint32_t UPDATE_RVA = 0x21B920;
    constexpr std::array<uint8_t, 9> UPDATE_ENTRY = {0x55, 0x8B, 0xEC, 0x57, 0x8B, 0x7D, 0x08, 0x8B, 0xCF};
    const auto address = imports::GameModuleBase() + UPDATE_RVA;
    if (!std::ranges::equal(UPDATE_ENTRY, std::span{reinterpret_cast<const uint8_t*>(address), UPDATE_ENTRY.size()})) {
        utils::GetLogger("hooks.drlg_activate")
            ->warn("DRLGACTIVATE_Update at {:#x} differs from 1.14d; fix skipped", address);
        return;
    }
    updateHook.SetTarget(reinterpret_cast<decltype(&UpdateHook)>(address));
    if (const auto error = core::detour::AttachAll({&updateHook}); error != 0) {
        utils::GetLogger("hooks.drlg_activate")->error("DRLGACTIVATE_Update detour failed: {}", error);
        return;
    }
    installed = true;
}
void Remove() {
    if (!installed) {
        return;
    }
    if (const auto error = core::detour::DetachAll({&updateHook}); error != 0) {
        utils::GetLogger("hooks.drlg_activate")->error("DRLGACTIVATE_Update detach failed: {}", error);
        return;
    }
    installed = false;
}
}  // namespace d2bs::lod114d::hooks::drlg_activate
