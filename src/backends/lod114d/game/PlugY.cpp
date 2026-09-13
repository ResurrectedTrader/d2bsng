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
#include "utils/Profiling.h"
#include "utils/utils.h"

#include <D2Inventory.h>   // D2ItemExtraDataStrc
#include <D2Items.h>       // IMODE_STORED
#include <D2StatList.h>    // STAT_GOLD
#include <Units/Item.h>    // D2ItemDataStrc
#include <Units/Player.h>  // D2PlayerDataStrc
#include <Units/Units.h>   // D2UnitStrc

#include <Windows.h>

#include <Psapi.h>

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
#include <thread>
#include <vector>

#pragma comment(lib, "version.lib")

namespace d2bs::imports::extras::plugy {

std::string Stash::Name() const {
    if (name == nullptr || name[0] == '\0') {
        return {};
    }
    return utils::ToStr(utils::ToWStr(name, CP_ACP));
}

D2UnitStrc* PYPlayerData::FirstItem(const Stash& page) const {
    if (!IsActivePage(page)) {
        return page.ptListItem;
    }
    auto* player = d2client::UNITS_GetPlayerUnit();
    if (player == nullptr || player->pInventory == nullptr) {
        return nullptr;
    }
    return d2common::INVENTORY_GetFirstItem(player->pInventory);
}

D2UnitStrc* PYPlayerData::NextItem(D2UnitStrc* item) {
    return d2common::INVENTORY_GetNextItem(item);
}

bool PYPlayerData::IsStashItem(const D2UnitStrc* item) {
    return item->pItemData != nullptr &&
           static_cast<game::ItemLocation>(item->pItemData->pExtraData.nNodePos) == game::ItemLocation::Stash;
}

std::vector<uint8_t> PYPlayerData::PlanSwitch(PageRef from, PageRef to) const {
    std::vector<uint8_t> commands;
    if (to.index >= PageCount(to.kind)) {
        return commands;
    }
    PageRef cur = from;
    if (cur.kind != to.kind) {
        commands.push_back(to.kind == game::StashTabKind::Shared ? CMD_SELECT_SHARED : CMD_SELECT_PERSONAL);
        cur = {.kind = to.kind, .index = 0};
    }
    if (to.index < cur.index) {
        const uint32_t back = cur.index - to.index;
        if (to.index < back) {
            commands.push_back(CMD_SELECT_FIRST);
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
// Item ids seen while one click's acknowledgement is awaited; a frame carries a
// handful of item packets at most.
constexpr size_t ACK_ID_CAPACITY = 64;

struct Version {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t build = 0;

    bool operator==(const Version&) const = default;

    // From the module's VERSIONINFO resource.
    static std::optional<Version> Read(HMODULE module);
    // PlugY displays FILEVERSION 14,0,3 as "14.03".
    std::string ToString() const;
};

// The official releases that carry the 1.14d port. For each, PlugY's git
// history was checked and everything read or sent here is identical: the
// PYPlayerData / Stash layouts, the allocation patch site and its original
// bytes, the size-immediate derivation, the 0x3A command values, the 0x9D
// packet layout, the handler patch sites, and the page-list semantics
// (docs/plugy_stash.md lists the checklist). An unknown build keeps the feature
// off rather than risk misreading memory.
constexpr std::array SUPPORTED_VERSIONS = {
    Version{.major = 12, .minor = 0, .build = 0}, Version{.major = 14, .minor = 0, .build = 0},
    Version{.major = 14, .minor = 0, .build = 1}, Version{.major = 14, .minor = 0, .build = 2},
    Version{.major = 14, .minor = 0, .build = 3},
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

std::string Version::ToString() const {
    return fmt::format("{}.{}{}", major, minor, build);
}

std::optional<Version> Version::Read(HMODULE module) {
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), path.size());
    if (length == 0 || length >= path.size()) {
        return std::nullopt;
    }
    const DWORD size = GetFileVersionInfoSizeW(path.data(), nullptr);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> buffer(size);
    if (GetFileVersionInfoW(path.data(), 0, size, buffer.data()) == 0) {
        return std::nullopt;
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLength = 0;
    if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &infoLength) == 0 || info == nullptr ||
        infoLength < sizeof(VS_FIXEDFILEINFO)) {
        return std::nullopt;
    }
    return Version{.major = HIWORD(info->dwFileVersionMS),
                   .minor = LOWORD(info->dwFileVersionMS),
                   .build = HIWORD(info->dwFileVersionLS)};
}

std::string VersionLabel(HMODULE module) {
    const auto version = Version::Read(module);
    return version ? version->ToString() : std::string("(unknown version)");
}

bool IsInsideModule(HMODULE module, uintptr_t address) {
    MODULEINFO info{};
    if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) == 0) {
        return false;
    }
    const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
    return address >= base && address < base + info.SizeOfImage;
}

template <typename T>
T ReadValue(uintptr_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

Detection Detect(HMODULE module) {
    const auto version = Version::Read(module);
    if (!version) {
        Logger()->warn("PlugY.dll is loaded but has no readable version resource; stash tabs disabled");
        return Detection::Inactive;
    }
    if (std::ranges::find(SUPPORTED_VERSIONS, *version) == SUPPORTED_VERSIONS.end()) {
        Logger()->warn("version {} is not a known release (supported: 12.00, 14.00 - 14.03); stash tabs disabled",
                       version->ToString());
        return Detection::Inactive;
    }

    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t callSite = base + ALLOC_CALL_RVA;
    if (ReadValue<uint8_t>(callSite) != OPCODE_CALL_REL32) {
        Logger()->warn("{}: unexpected code at InitPlayerData+{:#x}; stash tabs disabled", version->ToString(),
                       ALLOC_CALL_RVA - INIT_PLAYER_DATA_RVA);
        return Detection::Inactive;
    }
    const auto displacement = ReadValue<int32_t>(callSite + 1);
    const uintptr_t callTarget = callSite + REL32_INSN_LEN + static_cast<uintptr_t>(displacement);
    if (!IsInsideModule(module, callTarget)) {
        return Detection::NotYetPatched;
    }

    const uintptr_t sizeSite = base + ALLOC_SIZE_MOV_RVA;
    if (ReadValue<uint8_t>(sizeSite) != OPCODE_MOV_EDX_IMM32) {
        Logger()->warn("{}: unexpected code at InitPlayerData+{:#x}; stash tabs disabled", version->ToString(),
                       ALLOC_SIZE_MOV_RVA - INIT_PLAYER_DATA_RVA);
        return Detection::Inactive;
    }
    const auto allocSize = ReadValue<uint32_t>(sizeSite + 1);
    if (allocSize != sizeof(D2PlayerDataStrc)) {
        Logger()->warn("{}: player data size {:#x} differs from the expected {:#x}; stash tabs disabled",
                       version->ToString(), allocSize, sizeof(D2PlayerDataStrc));
        return Detection::Inactive;
    }

    Logger()->info("{} detected, multi-page stash tabs enabled", version->ToString());
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

// Polls `ready` until it holds or `timeout` passes. Releases this thread's read
// locks while sleeping so the game thread can run the frames that deliver the
// server's replies; `ready` takes its own read lock if it needs one.
template <typename Ready>
bool PollUntil(std::chrono::milliseconds timeout, const Ready& ready) {
    const GameReadLockReleaser releaser;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const profiling::ScopedSleep waiting;
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
    return true;
}

bool WaitForPage(PageRef target) {
    return PollUntil(PAGE_SWITCH_TIMEOUT, [target] {
        GameReadLock guard;
        const auto* ext = Extension();
        return ext != nullptr && ext->currentStash != nullptr && ext->ActivePage() == target;
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
        if (ext == nullptr || ext->currentStash == nullptr) {
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
    return WaitForPage(to);
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

// Item ids the server has named since the current click was armed. Written on the
// game thread by the packet observer, read by the one script thread inside
// WithActivePage (its operation mutex serialises clicks); ackMutex guards the vector.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex ackMutex;
std::vector<uint32_t> ackSeenIds;
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
    const std::scoped_lock lock(ackMutex);
    if (ackSeenIds.size() < ACK_ID_CAPACITY) {
        ackSeenIds.push_back(itemId);
    }
}

bool SawAckFor(std::span<const uint32_t> itemIds) {
    const std::scoped_lock lock(ackMutex);
    return std::ranges::any_of(
        itemIds, [](uint32_t id) { return id != 0 && std::ranges::find(ackSeenIds, id) != ackSeenIds.end(); });
}

// The client applies an item click locally and only then tells the server, and
// a page switch sent right behind the click can overtake it: the server would
// then park the page before it sees the pick-up, reject the pick-up, and leave
// the client holding an item the server still has in the stash. Every stash click
// that does anything (pick up, drop, swap) changes what is on the cursor, so that
// is both the "did it send anything" test and the id set to wait for: the item
// that left or reached the cursor is the one the server's item packet names.
ClickResult ClickAndAwaitAck(const std::function<ClickResult()>& action) {
    {
        const std::scoped_lock lock(ackMutex);
        ackSeenIds.clear();
    }
    hooks::intercepts::SetIncomingPacketObserver(&OnIncomingPacket);
    const auto cursorItemId = [] {
        GameReadLock guard;
        const auto cursor = Unit::CursorItem();
        return cursor ? cursor->Id() : 0U;
    };
    const uint32_t cursorBefore = cursorItemId();
    const ClickResult result = action();
    const uint32_t cursorAfter = cursorItemId();
    if (result == ClickResult::Dispatched && cursorAfter != cursorBefore) {
        const std::array ids = {cursorBefore, cursorAfter};
        if (!PollUntil(ITEM_ACK_TIMEOUT, [&ids] { return SawAckFor(ids); })) {
            Logger()->warn("no server acknowledgement for the stash click within {} ms", ITEM_ACK_TIMEOUT.count());
        }
    }
    hooks::intercepts::SetIncomingPacketObserver(nullptr);
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
    if (ReadValue<uint8_t>(site) != OPCODE_CALL_INDIRECT[0] ||
        ReadValue<uint8_t>(site + 1) != OPCODE_CALL_INDIRECT[1]) {
        return;
    }
    const auto slot = ReadValue<uintptr_t>(site + 2);
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = kernel32 != nullptr ? GetProcAddress(kernel32, "LoadLibraryA") : nullptr;
    if (loadLibrary == nullptr || ReadValue<uintptr_t>(slot) != reinterpret_cast<uintptr_t>(loadLibrary)) {
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
    return ext != nullptr && ext->currentStash != nullptr;
}

bool IsParkedItem(const D2UnitStrc* item) {
    return item->dwItemMode == IMODE_STORED && item->pItemData != nullptr &&
           item->pItemData->pExtraData.pParentInv == nullptr;
}

uint32_t PageCount(StashTabKind kind) {
    const auto* ext = Extension();
    return ext != nullptr && ext->currentStash != nullptr ? ext->PageCount(kind) : 0U;
}

bool HasPage(StashTabKind kind, uint32_t index) {
    const auto* ext = Extension();
    return ext != nullptr && ext->currentStash != nullptr && ext->FindPage(kind, index) != nullptr;
}

std::string PageName(StashTabKind kind, uint32_t index) {
    const auto* ext = Extension();
    const Stash* page = ext != nullptr && ext->currentStash != nullptr ? ext->FindPage(kind, index) : nullptr;
    return page != nullptr ? page->Name() : std::string{};
}

uint32_t SharedGold() {
    const auto* ext = Extension();
    return ext != nullptr && ext->currentStash != nullptr && ext->sharedStash != nullptr ? ext->sharedGold : 0U;
}

std::vector<Unit> GetPageItems(StashTabKind kind, uint32_t index) {
    std::vector<Unit> items;
    const auto* ext = Extension();
    if (ext == nullptr || ext->currentStash == nullptr) {
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
    if (ext == nullptr || ext->currentStash == nullptr) {
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
        if (const auto* ext = Extension(); ext != nullptr && ext->currentStash != nullptr) {
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
    if (!SwitchTo(target, *original)) {
        Logger()->warn("stash page {}:{} could not be restored after the click", static_cast<uint32_t>(original->kind),
                       original->index);
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
        if (ext == nullptr || ext->currentStash == nullptr || ext->sharedStash == nullptr) {
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
