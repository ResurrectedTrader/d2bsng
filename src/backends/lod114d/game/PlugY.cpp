#include "PlugY.h"

#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/Unit.h"
#include "hooks/InlinePatch.h"
#include "hooks/Intercepts.h"
#include "imports/D2Client.h"
#include "imports/D2Common.h"
#include "imports/extras/PlugY.h"
#include "utils/DeferGuard.h"
#include "utils/utils.h"

#include <D2Inventory.h>   // D2ItemExtraDataStrc
#include <D2Items.h>       // IMODE_STORED
#include <D2StatList.h>    // STAT_GOLD
#include <Units/Item.h>    // D2ItemDataStrc
#include <Units/Player.h>  // D2PlayerDataStrc
#include <Units/Units.h>   // D2UnitStrc

#include <Windows.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace d2bs::imports::extras::plugy {

std::string Stash::Name() const {
    if (name == nullptr || name[0] == '\0') {
        return {};
    }
    return utils::ToStr(utils::ToWStr(name, CP_ACP));
}

void PYPlayerData::ForEachItem(const Stash& page, const std::function<void(D2UnitStrc*)>& fn) const {
    if (!HasStashTabs()) {
        return;
    }
    const bool isActive = &page == currentStash;
    D2UnitStrc* first = page.ptListItem;
    if (isActive) {
        auto* player = d2client::UNITS_GetPlayerUnit();
        first = player != nullptr && player->pInventory != nullptr
                    ? d2common::INVENTORY_GetFirstItem(player->pInventory)
                    : nullptr;
    }
    for (auto* item = first; item != nullptr; item = d2common::INVENTORY_GetNextItem(item)) {
        if (isActive &&
            (item->pItemData == nullptr ||
             static_cast<game::ItemLocation>(item->pItemData->pExtraData.nNodePos) != game::ItemLocation::Stash)) {
            continue;
        }
        fn(item);
    }
}

std::vector<uint8_t> PYPlayerData::PlanSwitch(PageRef from, PageRef to) const {
    std::vector<uint8_t> commands;
    if (to.index >= PageCount(to.kind)) {
        return commands;
    }
    // The kind-select commands land on that kind's first page whether or not the
    // kind changes, so they double as "jump to first". PlugY's own first-page
    // command (0x1F) is not used: it picks the kind from the server's
    // showSharedStash flag, which only the kind-select commands set, so it can
    // land on the other kind's first page. The kind-select commands are ignored
    // while the shared stash is disabled, so without one it is singles only.
    const bool hasKindSelect = sharedStash != nullptr;
    const uint8_t selectKind = to.kind == game::StashTabKind::Shared ? CMD_SELECT_SHARED : CMD_SELECT_PERSONAL;
    PageRef cur = from;
    if (cur.kind != to.kind) {
        commands.push_back(selectKind);
        cur = {.kind = to.kind, .index = 0};
    }
    if (to.index < cur.index) {
        const uint32_t back = cur.index - to.index;
        if (hasKindSelect && to.index < back) {
            commands.push_back(selectKind);
            cur.index = 0;
        } else {
            commands.insert(commands.end(), back, CMD_SELECT_PREVIOUS);
            cur.index = to.index;
        }
    }
    commands.insert(commands.end(), to.index - cur.index, CMD_SELECT_NEXT);
    return commands;
}

}  // namespace d2bs::imports::extras::plugy

namespace d2bs::game::plugy {

namespace {

using imports::extras::plugy::CMD_PUT_GOLD;
using imports::extras::plugy::CMD_TAKE_GOLD;
using imports::extras::plugy::PACKET_SPEND_STAT_POINT;
using imports::extras::plugy::PageRef;
using imports::extras::plugy::PYPlayerData;
using imports::extras::plugy::Stash;

// D2Common's InitPlayerData in 1.14d Game.exe. Its allocation call is the one
// PlugY redirects to grow the block by sizeof(PYPlayerData):
//   +0x47  BA 6C 01 00 00   mov edx, sizeof(D2PlayerDataStrc)
//   +0x4C  E8 xx xx xx xx   call Fog alloc  (rel32 rewritten to point into PlugY.dll)
// PlugY reads the same immediate to place its extension, so reading it here
// guarantees we land where PlugY did.
constexpr uint32_t INIT_PLAYER_DATA_RVA = 0x221F90;
constexpr uint32_t ALLOC_SIZE_MOV_RVA = INIT_PLAYER_DATA_RVA + 0x47;
constexpr uint32_t ALLOC_CALL_RVA = INIT_PLAYER_DATA_RVA + 0x4C;
constexpr uint8_t OPCODE_MOV_EDX_IMM32 = 0xBA;
constexpr uint8_t OPCODE_CALL_REL32 = 0xE8;
constexpr size_t REL32_INSN_LEN = 5;

// Game.exe's startup loads advapi32 through this `call [IAT LoadLibraryA]`. It is
// where PlugY.exe's stub runs PlugY's Init: Fog's memory pool already exists (Init
// allocates through it), and nothing PlugY's startup-time features hook has run
// yet. Verified against 1.14d: FF 15 44 C1 6C 00.
constexpr uint32_t STARTUP_LOADLIBRARY_CALL_RVA = 0x621C;
constexpr std::array<uint8_t, 2> OPCODE_CALL_INDIRECT = {0xFF, 0x15};
constexpr size_t IAT_CALL_LEN = 6;

// Server -> client item action packets (0x9C: item on the ground / the player,
// 0x9D: item on an owned unit), item id at offset 4. PlugY reuses id 0x9D for its
// own page updates with a function byte from 0x18 up, above the vanilla action
// range, so the action byte tells the two apart.
constexpr uint8_t PACKET_ITEM_ACTION = 0x9C;
constexpr uint8_t PACKET_ITEM_ACTION_OWNED = 0x9D;
constexpr size_t ITEM_ACTION_ID_OFFSET = 4;
constexpr size_t ITEM_ACTION_MIN_SIZE = ITEM_ACTION_ID_OFFSET + sizeof(uint32_t);
constexpr uint8_t FIRST_PLUGY_FUNC = 0x18;

constexpr std::chrono::milliseconds PAGE_SWITCH_TIMEOUT{3000};
constexpr std::chrono::milliseconds ITEM_ACK_TIMEOUT{2000};
constexpr std::chrono::milliseconds POLL_INTERVAL{5};

// The official releases that carry the 1.14d port. For each, PlugY's git
// history was checked and everything read or sent here is identical: the
// PYPlayerData / Stash layouts, the allocation patch site and its original
// bytes, the size-immediate derivation, the 0x3A command values, the 0x9D
// packet layout, the handler patch sites, and the page-list semantics
// (docs/plugy_stash.md lists the checklist). An unknown build keeps the feature
// off rather than risk misreading memory.
constexpr std::array SUPPORTED_VERSIONS = {
    utils::ModuleVersion{.major = 12, .minor = 0, .build = 0},
    utils::ModuleVersion{.major = 14, .minor = 0, .build = 0},
    utils::ModuleVersion{.major = 14, .minor = 0, .build = 1},
    utils::ModuleVersion{.major = 14, .minor = 0, .build = 2},
    utils::ModuleVersion{.major = 14, .minor = 0, .build = 3},
};

constexpr std::array KINDS = {StashTabKind::Personal, StashTabKind::Shared};

enum class Detection : uint8_t {
    Active,
    Inactive,
    // The module is a supported build but its Init has not redirected the
    // allocation yet; on the manager-injection path that happens at Game.exe's
    // startup LoadLibraryA, so this is not a final answer.
    NotYetPatched,
};

std::shared_ptr<spdlog::logger>& Logger() {
    static auto logger = utils::GetLogger("plugy");
    return logger;
}

// PlugY displays FILEVERSION 14,0,3 as "14.03".
std::string Label(const utils::ModuleVersion& version) {
    return fmt::format("{}.{}{}", version.major, version.minor, version.build);
}

std::string VersionLabel(HMODULE module) {
    const auto version = utils::GetModuleVersion(module);
    return version ? Label(*version) : "(unknown version)";
}

Detection Detect(HMODULE module) {
    const auto version = utils::GetModuleVersion(module);
    if (!version) {
        Logger()->warn("PlugY.dll is loaded but has no readable version resource; stash tabs disabled");
        return Detection::Inactive;
    }
    if (std::ranges::find(SUPPORTED_VERSIONS, *version) == SUPPORTED_VERSIONS.end()) {
        Logger()->warn("version {} is not a known release (supported: 12.00, 14.00 - 14.03); stash tabs disabled",
                       Label(*version));
        return Detection::Inactive;
    }

    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t callSite = base + ALLOC_CALL_RVA;
    if (utils::ReadValue<uint8_t>(callSite) != OPCODE_CALL_REL32) {
        Logger()->warn("{}: unexpected code at InitPlayerData+{:#x}; stash tabs disabled", Label(*version),
                       ALLOC_CALL_RVA - INIT_PLAYER_DATA_RVA);
        return Detection::Inactive;
    }
    const auto displacement = utils::ReadValue<int32_t>(callSite + 1);
    const uintptr_t callTarget = callSite + REL32_INSN_LEN + static_cast<uintptr_t>(displacement);
    if (!utils::IsInsideModule(module, callTarget)) {
        return Detection::NotYetPatched;
    }

    const uintptr_t sizeSite = base + ALLOC_SIZE_MOV_RVA;
    if (utils::ReadValue<uint8_t>(sizeSite) != OPCODE_MOV_EDX_IMM32) {
        Logger()->warn("{}: unexpected code at InitPlayerData+{:#x}; stash tabs disabled", Label(*version),
                       ALLOC_SIZE_MOV_RVA - INIT_PLAYER_DATA_RVA);
        return Detection::Inactive;
    }
    const auto allocSize = utils::ReadValue<uint32_t>(sizeSite + 1);
    if (allocSize != sizeof(D2PlayerDataStrc)) {
        Logger()->warn("{}: player data size {:#x} differs from the expected {:#x}; stash tabs disabled",
                       Label(*version), allocSize, sizeof(D2PlayerDataStrc));
        return Detection::Inactive;
    }

    Logger()->info("{} detected, multi-page stash tabs enabled", Label(*version));
    return Detection::Active;
}

// PlugY's extension sits right after the game's own player data block. Only
// valid once IsActive() has returned true: without the patch those bytes belong
// to whatever the Fog pool placed next.
const PYPlayerData* Extension() {
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr || player->pPlayerData == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<const PYPlayerData*>(reinterpret_cast<uintptr_t>(player->pPlayerData) +
                                                 sizeof(D2PlayerDataStrc));
}

void SendCommand(uint8_t command) {
    const std::array<uint8_t, 3> packet = {PACKET_SPEND_STAT_POINT, command, 0};
    SendGamePacket(packet);
}

bool WaitForPage(PageRef target) {
    return PollUntil(PAGE_SWITCH_TIMEOUT, POLL_INTERVAL, [target] {
        GameReadLock guard;
        const auto* ext = Extension();
        return ext != nullptr && ext->ActivePage() == target;
    });
}

bool SwitchTo(PageRef from, PageRef to) {
    if (from == to) {
        return true;
    }
    std::vector<uint8_t> plan;
    {
        GameReadLock guard;
        const auto* ext = Extension();
        if (ext == nullptr) {
            return false;
        }
        plan = ext->PlanSwitch(from, to);
    }
    if (plan.empty()) {
        return false;
    }
    for (const auto command : plan) {
        SendCommand(command);
    }
    if (WaitForPage(to)) {
        return true;
    }
    std::optional<PageRef> actual;
    {
        GameReadLock guard;
        if (const auto* ext = Extension(); ext != nullptr) {
            actual = ext->ActivePage();
        }
    }
    Logger()->debug("stash page {}:{} -> {}:{} did not complete within {} ms ({} command(s) sent); mirror shows {}",
                    static_cast<uint32_t>(from.kind), from.index, static_cast<uint32_t>(to.kind), to.index,
                    PAGE_SWITCH_TIMEOUT.count(), plan.size(),
                    actual ? fmt::format("{}:{}", static_cast<uint32_t>(actual->kind), actual->index) : "no page");
    return false;
}

// PlugY's exported Init, run once. Reads PlugY.ini relative to the current
// directory, which the launcher guarantees is the game folder; guarantee the same.
void RunInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE module = GetModuleHandleW(L"PlugY.dll");
        if (module == nullptr) {
            return;
        }
        // PlugY's launcher resolves the entry by this decorated name (extern "C" __stdcall, one pointer arg).
        auto* proc = GetProcAddress(module, "_Init@4");
        if (proc == nullptr) {
            Logger()->warn("PlugY.dll is loaded but exports no Init; leaving it uninitialised");
            return;
        }
        auto init = reinterpret_cast<void*(__stdcall*)(LPSTR)>(proc);

        std::array<wchar_t, MAX_PATH> exePath{};
        const DWORD exeLength = GetModuleFileNameW(nullptr, exePath.data(), exePath.size());
        std::array<wchar_t, MAX_PATH> previousDir{};
        const DWORD previousLength = GetCurrentDirectoryW(previousDir.size(), previousDir.data());
        const bool redirect =
            exeLength != 0 && exeLength < exePath.size() && previousLength != 0 && previousLength < previousDir.size();
        if (redirect) {
            SetCurrentDirectoryW(std::filesystem::path(exePath.data()).parent_path().c_str());
        }

        Logger()->info("PlugY.dll {} is loaded; running its Init from the game's startup", VersionLabel(module));
        init(nullptr);

        if (redirect) {
            SetCurrentDirectoryW(previousDir.data());
        }
    });
}

// Replacement for Game.exe's startup `call [LoadLibraryA]` (see InstallInitHook):
// same stdcall shape as the original import, so the caller's stack and EAX are
// exactly what it expects; runs PlugY's Init first, as PlugY.exe's stub does.
extern "C" HMODULE __stdcall StartupLoadLibrary(LPCSTR name) {
    RunInit();
    return LoadLibraryA(name);
}

// The one or two item ids the current click is waiting for the server to name,
// and whether it has. The one script thread inside WithActivePage (serialised by
// its operation mutex) sets the targets before arming the observer and reads the
// flag; the packet observer sets the flag on the game thread. All atomic, so the
// observer holds no lock and cannot match against a partially-written target.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<uint32_t> ackTargetA{0};
std::atomic<uint32_t> ackTargetB{0};
std::atomic<bool> ackSeen{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void OnIncomingPacket(std::span<const uint8_t> packet) {
    if (packet.size() < ITEM_ACTION_MIN_SIZE) {
        return;
    }
    const uint8_t id = packet[0];
    if (id != PACKET_ITEM_ACTION && (id != PACKET_ITEM_ACTION_OWNED || packet[1] >= FIRST_PLUGY_FUNC)) {
        return;
    }
    uint32_t itemId = 0;
    std::memcpy(&itemId, packet.data() + ITEM_ACTION_ID_OFFSET, sizeof(itemId));
    if (itemId != 0 && (itemId == ackTargetA.load(std::memory_order_relaxed) ||
                        itemId == ackTargetB.load(std::memory_order_relaxed))) {
        ackSeen.store(true, std::memory_order_release);
    }
}

// The client applies an item click locally and only then tells the server, and
// a page switch sent right behind the click can overtake it: the server would
// then park the page before it sees the pick-up, reject the pick-up, and leave
// the client holding an item the server still has in the stash. Every stash click
// that does anything (pick up, drop, swap) changes what is on the cursor, so that
// is both the "did it send anything" test and the id set to wait for: the item
// that left or reached the cursor is the one the server's item packet names.
ClickResult ClickAndAwaitAck(const std::function<ClickResult()>& action) {
    const auto cursorItemId = [] {
        GameReadLock guard;
        const auto cursor = Unit::CursorItem();
        return cursor ? cursor->Id() : 0U;
    };
    const uint32_t cursorBefore = cursorItemId();
    const ClickResult result = action();
    const uint32_t cursorAfter = cursorItemId();
    if (result == ClickResult::Dispatched && cursorAfter != cursorBefore) {
        // The server's item packet names the item that left or reached the cursor;
        // wait for exactly that id. Publish the targets and clear the flag before
        // arming the observer, whose store-release install orders them ahead of any
        // packet the game thread then delivers.
        ackSeen.store(false, std::memory_order_relaxed);
        ackTargetA.store(cursorBefore, std::memory_order_relaxed);
        ackTargetB.store(cursorAfter, std::memory_order_relaxed);
        hooks::intercepts::SetIncomingPacketObserver(&OnIncomingPacket);
        if (!PollUntil(ITEM_ACK_TIMEOUT, POLL_INTERVAL, [] { return ackSeen.load(std::memory_order_acquire); })) {
            Logger()->warn("no server acknowledgement for the stash click within {} ms", ITEM_ACK_TIMEOUT.count());
        }
        hooks::intercepts::SetIncomingPacketObserver(nullptr);
        ackTargetA.store(0, std::memory_order_relaxed);
        ackTargetB.store(0, std::memory_order_relaxed);
    }
    return result;
}

}  // namespace

void InstallInitHook() {
    HMODULE module = GetModuleHandleW(L"PlugY.dll");
    if (module == nullptr) {
        return;
    }
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t site = base + STARTUP_LOADLIBRARY_CALL_RVA;
    // `FF 15 <slot>`: call through the IAT slot that must hold LoadLibraryA. Anything
    // else means PlugY.exe already redirected this call (its stub handles Init) or
    // the site is not what we expect; either way leave it alone.
    if (utils::ReadValue<uint8_t>(site) != OPCODE_CALL_INDIRECT[0] ||
        utils::ReadValue<uint8_t>(site + 1) != OPCODE_CALL_INDIRECT[1]) {
        return;
    }
    const auto slot = utils::ReadValue<uintptr_t>(site + 2);
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = kernel32 != nullptr ? GetProcAddress(kernel32, "LoadLibraryA") : nullptr;
    if (loadLibrary == nullptr || utils::ReadValue<uintptr_t>(slot) != reinterpret_cast<uintptr_t>(loadLibrary)) {
        return;
    }
    std::array<uint8_t, IAT_CALL_LEN> original{};
    hooks::WriteCallN(site, reinterpret_cast<uintptr_t>(&StartupLoadLibrary), IAT_CALL_LEN, original.data());
}

bool IsActive() {
    // -1 undecided, 0 off, 1 on. Only the undecided path takes the mutex.
    static std::atomic<int8_t> verdict{-1};
    if (const auto known = verdict.load(std::memory_order_acquire); known >= 0) {
        return known == 1;
    }
    static std::mutex mutex;
    const std::scoped_lock lock(mutex);
    if (const auto known = verdict.load(std::memory_order_acquire); known >= 0) {
        return known == 1;
    }
    // PlugY.dll is loaded at Game.exe's startup (by PlugY.exe or by the manager
    // ahead of d2bs) and its Init runs there too, long before a player unit exists.
    // So with a player in the game, a missing module or a missing patch is final;
    // before that, both are retried.
    const bool inGame = imports::d2client::UNITS_GetPlayerUnit() != nullptr;
    HMODULE module = GetModuleHandleW(L"PlugY.dll");
    if (module == nullptr) {
        if (inGame) {
            Logger()->debug("PlugY.dll is not loaded; stash tabs are the vanilla single tab");
            verdict.store(0, std::memory_order_release);
        }
        return false;
    }
    switch (Detect(module)) {
        case Detection::Active:
            verdict.store(1, std::memory_order_release);
            return true;
        case Detection::Inactive:
            verdict.store(0, std::memory_order_release);
            return false;
        case Detection::NotYetPatched:
            if (inGame) {
                Logger()->info("{} loaded without the multi-page stash (ActiveMultiPageStash=0); stash tabs disabled",
                               VersionLabel(module));
                verdict.store(0, std::memory_order_release);
            }
            return false;
    }
    return false;
}

bool HasStashTabs() {
    GameReadLock guard;
    const auto* ext = Extension();
    return ext != nullptr && ext->HasStashTabs();
}

bool IsParkedItem(const D2UnitStrc* item) {
    return item->dwItemMode == IMODE_STORED && item->pItemData != nullptr &&
           item->pItemData->pExtraData.pParentInv == nullptr;
}

uint32_t PageCount(StashTabKind kind) {
    const auto* ext = Extension();
    return ext != nullptr ? ext->PageCount(kind) : 0U;
}

bool HasPage(StashTabKind kind, uint32_t index) {
    const auto* ext = Extension();
    return ext != nullptr && ext->FindPage(kind, index) != nullptr;
}

std::string PageName(StashTabKind kind, uint32_t index) {
    const auto* ext = Extension();
    const Stash* page = ext != nullptr ? ext->FindPage(kind, index) : nullptr;
    return page != nullptr ? page->Name() : std::string{};
}

uint32_t SharedGold() {
    const auto* ext = Extension();
    return ext != nullptr ? ext->SharedGold() : 0U;
}

std::vector<Unit> GetPageItems(StashTabKind kind, uint32_t index) {
    std::vector<Unit> items;
    const auto* ext = Extension();
    if (ext == nullptr) {
        return items;
    }
    const Stash* page = ext->FindPage(kind, index);
    if (page == nullptr) {
        return items;
    }
    ext->ForEachItem(*page, [&](D2UnitStrc* item) { items.push_back(Unit::FromPtr(item)); });
    return items;
}

std::optional<StashTab> FindPage(const Unit& item) {
    const auto* ext = Extension();
    if (ext == nullptr) {
        return std::nullopt;
    }
    const uint32_t itemId = item.Id();
    for (const auto kind : KINDS) {
        uint32_t index = 0;
        const Stash* page = ext->ForEachPage(kind, [&](const Stash& candidate, uint32_t i) {
            bool found = false;
            ext->ForEachItem(candidate, [&](const D2UnitStrc* unit) { found |= unit->dwUnitId == itemId; });
            index = i;
            return found;
        });
        if (page != nullptr) {
            return StashTab(kind, index);
        }
    }
    return std::nullopt;
}

ClickResult WithActivePage(StashTabKind kind, uint32_t index, const std::function<ClickResult()>& action) {
    // The action may itself hand an item back here (ClickItem on a unit the mirror
    // already shows as active but the game still has detached); a second entry
    // would deadlock on the operation mutex below.
    static thread_local bool inProgress = false;
    if (inProgress) {
        return ClickResult::StashTabUnavailable;
    }
    inProgress = true;
    const DeferGuard reset([] { inProgress = false; });

    // Drop this thread's read locks before queueing behind another script's page
    // operation: that operation needs the game thread, and the game thread needs
    // the write lock.
    const GameReadLockReleaser releaser;
    static std::mutex operationMutex;
    const std::scoped_lock lock(operationMutex);

    std::optional<PageRef> original;
    {
        GameReadLock guard;
        if (const auto* ext = Extension(); ext != nullptr) {
            original = ext->ActivePage();
        }
    }
    if (!original) {
        return ClickResult::StashTabUnavailable;
    }
    const PageRef target{.kind = kind, .index = index};
    if (target == *original) {
        return action();
    }
    if (!SwitchTo(*original, target)) {
        Logger()->warn("stash page {}:{} could not be made active", static_cast<uint32_t>(kind), index);
        return ClickResult::StashTabUnavailable;
    }
    const ClickResult result = ClickAndAwaitAck(action);
    // Best effort: PlugY reselects the page of the item the click touched, so a
    // restore that deposited an item (drop or swap) loses this race by design. The
    // click already happened; which page ends up shown is not part of the contract.
    if (!SwitchTo(target, *original)) {
        Logger()->debug("stash page {}:{} not restored after the click; PlugY reselected the clicked item's page",
                        static_cast<uint32_t>(original->kind), original->index);
    }
    return result;
}

ClickResult ClickParkedItem(ClickButton button, const Unit& item) {
    std::optional<StashTab> tab;
    {
        GameReadLock guard;
        tab = FindPage(item);
    }
    if (!tab) {
        return ClickResult::InvalidTarget;
    }
    return WithActivePage(tab->Kind(), tab->Index(), [&] { return ClickItem(button, item); });
}

bool MoveSharedGold(GoldActionMode mode) {
    {
        GameReadLock guard;
        const auto* ext = Extension();
        if (ext == nullptr || !ext->HasSharedStash()) {
            return false;
        }
        const auto carried = Unit::Player().GetStat(STAT_GOLD);
        if (mode == GoldActionMode::Deposit && (carried <= 0 || ext->sharedGold == UINT32_MAX)) {
            return false;
        }
        if (mode == GoldActionMode::Withdraw && ext->sharedGold == 0) {
            return false;
        }
    }
    // Fire and forget, like the panel's own button: the server applies the move
    // and its UC_SHARED_GOLD reply updates the mirror whenever it arrives.
    SendCommand(mode == GoldActionMode::Deposit ? CMD_PUT_GOLD : CMD_TAKE_GOLD);
    return true;
}

}  // namespace d2bs::game::plugy
