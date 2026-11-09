#include "hooks/LvlSubFixes.h"

#include "detour/Hook.h"
#include "imports/D2Common.h"
#include "utils/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <Drlg/D2DrlgTileSub.h>
#pragma clang diagnostic pop

#include <bit>
#include <cstddef>
#include <span>

namespace d2bs::lod114d::hooks::lvlsub {
namespace {
using namespace imports;
inline GameVar<D2LvlSubTxt*> rows{0x56CA10};
inline GameVar<int32_t> rowCount{0x56CA14};
inline FastcallFunc<D2LvlSubTxt*(int32_t)> firstRow{0x21FA90};
inline FastcallFunc<void(HD2ARCHIVE, D2LvlSubTxt*)> initializeFile{0x2704E0};
uintptr_t applyGroup = 0;

// The 1.14d helper at 0x66F990 takes the row in EBX and pops its stack argument.
// Keep only that physical ABI boundary in assembly; both walkers are ordinary C++.
void ApplyGroup(D2LvlSubTxt* row, D2UnkOutdoorStrc* outdoor) {
    __asm {
        mov ebx, row
        push outdoor
        call dword ptr [applyGroup]
    }
}

std::span<D2LvlSubTxt> RemainingRows(int32_t type) {
    auto* first = firstRow(type);
    auto* base = *rows;
    const int32_t count = *rowCount;
    // The getter retains the game's existing subtype validation. Bound its returned row too.
    const auto begin = reinterpret_cast<uintptr_t>(base);
    const auto at = reinterpret_cast<uintptr_t>(first);
    if (base == nullptr || count <= 0 || at < begin || (at - begin) % sizeof(D2LvlSubTxt) != 0) {
        return {};
    }
    const size_t index = (at - begin) / sizeof(D2LvlSubTxt);
    return index < static_cast<size_t>(count) ? std::span{first, static_cast<size_t>(count) - index}
                                              : std::span<D2LvlSubTxt>{};
}

void __fastcall Place(D2UnkOutdoorStrc* outdoor) {
    const auto archive = outdoor->pLevel->pDrlg->hArchive;
    for (auto& row : RemainingRows(outdoor->nLvlSubId)) {
        if (row.dwType != static_cast<uint32_t>(outdoor->nLvlSubId)) {
            break;
        }
        initializeFile(archive, &row);
        ApplyGroup(&row, outdoor);
    }
}

int32_t __fastcall Pick(D2DrlgRoomStrc* room, int32_t type, int32_t theme) {
    if (type == -1 || theme == -1) {
        return 0;
    }
    uint32_t selected = 0;
    uint32_t bit = 0;
    for (const auto& row : RemainingRows(type)) {
        if (row.dwType != static_cast<uint32_t>(type)) {
            break;
        }
        // Match the original low-DWORD modulo and signed probability comparison, including
        // x86's masked shift count. Do not substitute SEED_RollPercentage (64-bit modulo).
        const uint32_t roll = static_cast<uint32_t>(SEED_RollRandomNumber(&room->pSeed)) % 100U;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - native theme index
        if (static_cast<int32_t>(roll) < std::bit_cast<int32_t>(row.nProb[theme])) {
            selected |= 1U << (bit & 31U);
            room->dwDT1Mask |= row.dwDt1Mask;
        }
        ++bit;
    }
    return std::bit_cast<int32_t>(selected);
}

core::detour::Hook<decltype(&Place)> placeHook{&Place};
core::detour::Hook<decltype(&Pick)> pickHook{&Pick};
static_assert(sizeof(D2LvlSubTxt) == 0x15C);
}  // namespace

void Install() {
    const auto base = GameModuleBase();
    applyGroup = base + 0x26F990;
    placeHook.SetTarget(reinterpret_cast<decltype(&Place)>(base + 0x270750));
    pickHook.SetTarget(reinterpret_cast<decltype(&Pick)>(base + 0x2706A0));
    if (const auto error = core::detour::AttachAll({&placeHook, &pickHook}); error != 0) {
        utils::GetLogger("hooks.lvlsub")->error("LvlSub detours failed: {}", error);
    }
}

void Remove() {
    if (const auto error = core::detour::DetachAll({&placeHook, &pickHook}); error != 0) {
        utils::GetLogger("hooks.lvlsub")->error("LvlSub detach failed: {}", error);
    }
}
}  // namespace d2bs::lod114d::hooks::lvlsub
