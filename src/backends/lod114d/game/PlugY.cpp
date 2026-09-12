#include "PlugY.h"

#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/Unit.h"
#include "hooks/InlinePatch.h"
#include "hooks/Intercepts.h"
#include "imports/D2Client.h"
#include "imports/D2Common.h"
#include "imports/extras/PlugY.h"
#include "utils/Profiling.h"
#include "utils/utils.h"

#include <D2Inventory.h>   // D2ItemExtraDataStrc
#include <D2StatList.h>    // STAT_GOLD, STAT_GOLDBANK
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

namespace d2bs::game::plugy {

namespace {

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

// PlugY's client -> server channel: the vanilla 0x3A "spend stat point" packet
// (BYTE id, WORD param) carrying an out-of-range command in the low byte
// (PlugY/Commons/updatingConst.h). The server answers each with a 0x9D page
// update that the client applies to its mirror.
constexpr uint8_t PACKET_SPEND_STAT_POINT = 0x3A;
constexpr uint8_t CMD_SELECT_PREVIOUS = 0x19;
constexpr uint8_t CMD_SELECT_NEXT = 0x1A;
constexpr uint8_t CMD_SELECT_PERSONAL = 0x1B;  // first personal page
constexpr uint8_t CMD_SELECT_SHARED = 0x1C;    // first shared page
constexpr uint8_t CMD_SELECT_FIRST = 0x1F;     // first page of the current kind
constexpr uint8_t CMD_PUT_GOLD = 0x26;          // carried gold -> shared pool (all that fits)
constexpr uint8_t CMD_TAKE_GOLD = 0x27;         // shared pool -> carried gold (all that fits)

// Game.exe's startup loads advapi32 through this `call [IAT LoadLibraryA]`. It is
// where PlugY.exe's stub runs PlugY's Init: Fog's memory pool already exists (Init
// allocates through it), and nothing PlugY's startup-time features hook has run
// yet. Verified against 1.14d: FF 15 44 C1 6C 00.
constexpr uint32_t STARTUP_LOADLIBRARY_CALL_RVA = 0x621C;
constexpr std::array<uint8_t, 2> OPCODE_CALL_INDIRECT = {0xFF, 0x15};
constexpr size_t IAT_CALL_LEN = 6;

constexpr std::chrono::milliseconds PAGE_SWITCH_TIMEOUT{3000};
constexpr std::chrono::milliseconds PAGE_SWITCH_POLL{5};

// Server -> client item action packets (0x9C: item on the ground / the player,
// 0x9D: item on an owned unit), item id at offset 4. PlugY reuses id 0x9D for its
// own page updates with a function byte from 0x18 up, above the vanilla action
// range, so the action byte tells the two apart.
constexpr uint8_t PACKET_ITEM_ACTION = 0x9C;
constexpr uint8_t PACKET_ITEM_ACTION_OWNED = 0x9D;
constexpr size_t ITEM_ACTION_ID_OFFSET = 4;
constexpr size_t ITEM_ACTION_MIN_SIZE = ITEM_ACTION_ID_OFFSET + sizeof(uint32_t);
constexpr uint8_t FIRST_PLUGY_FUNC = 0x18;
constexpr std::chrono::milliseconds ITEM_ACK_TIMEOUT{2000};

struct Version {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t build = 0;

    bool operator==(const Version&) const = default;
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

// Guard for a corrupted or cyclic page list; PlugY itself has no practical limit.
constexpr uint32_t MAX_PAGES_WALKED = 1U << 16;

struct PageRef {
    StashTabKind kind = StashTabKind::Personal;
    uint32_t index = 0;

    bool operator==(const PageRef&) const = default;
};

std::shared_ptr<spdlog::logger>& Logger() {
    static auto logger = utils::GetLogger("plugy");
    return logger;
}

// PlugY displays FILEVERSION 14,0,3 as "14.03".
std::string Format(const Version& v) {
    return fmt::format("{}.{}{}", v.major, v.minor, v.build);
}

std::optional<Version> ReadVersion(HMODULE module) {
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

bool IsInsideModule(HMODULE module, uintptr_t address) {
    MODULEINFO info{};
    if (GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)) == 0) {
        return false;
    }
    const auto base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
    return address >= base && address < base + info.SizeOfImage;
}

uint8_t ReadByte(uintptr_t address) {
    return *reinterpret_cast<const uint8_t*>(address);
}

template <typename T>
T ReadValue(uintptr_t address) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

bool Detect(HMODULE module) {
    const auto version = ReadVersion(module);
    if (!version) {
        Logger()->warn("PlugY.dll is loaded but has no readable version resource; stash tabs disabled");
        return false;
    }
    if (std::ranges::find(SUPPORTED_VERSIONS, *version) == SUPPORTED_VERSIONS.end()) {
        Logger()->warn("version {} is not a known release (supported: 12.00, 14.00 - 14.03); stash tabs disabled",
                       Format(*version));
        return false;
    }

    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t callSite = base + ALLOC_CALL_RVA;
    if (ReadByte(callSite) != OPCODE_CALL_REL32) {
        Logger()->warn("{}: unexpected code at InitPlayerData+{:#x}; stash tabs disabled", Format(*version),
                       ALLOC_CALL_RVA - INIT_PLAYER_DATA_RVA);
        return false;
    }
    const auto displacement = ReadValue<int32_t>(callSite + 1);
    const uintptr_t callTarget = callSite + REL32_INSN_LEN + static_cast<uintptr_t>(displacement);
    if (!IsInsideModule(module, callTarget)) {
        Logger()->info("{} loaded without the multi-page stash (ActiveMultiPageStash=0); stash tabs disabled",
                       Format(*version));
        return false;
    }

    const uintptr_t sizeSite = base + ALLOC_SIZE_MOV_RVA;
    if (ReadByte(sizeSite) != OPCODE_MOV_EDX_IMM32) {
        Logger()->warn("{}: unexpected code at InitPlayerData+{:#x}; stash tabs disabled", Format(*version),
                       ALLOC_SIZE_MOV_RVA - INIT_PLAYER_DATA_RVA);
        return false;
    }
    const auto allocSize = ReadValue<uint32_t>(sizeSite + 1);
    if (allocSize != sizeof(D2PlayerDataStrc)) {
        Logger()->warn("{}: player data size {:#x} differs from the expected {:#x}; stash tabs disabled",
                       Format(*version), allocSize, sizeof(D2PlayerDataStrc));
        return false;
    }

    Logger()->info("{} detected, multi-page stash tabs enabled", Format(*version));
    return true;
}

// PlugY's extension sits right after the game's own player data block.
const PYPlayerData* Extension() {
    if (!IsActive()) {
        return nullptr;
    }
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr || player->pPlayerData == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<const PYPlayerData*>(reinterpret_cast<uintptr_t>(player->pPlayerData) +
                                                 sizeof(D2PlayerDataStrc));
}

// Extension() with a populated page mirror, else nullptr.
const PYPlayerData* Pages() {
    const auto* ext = Extension();
    return ext != nullptr && ext->currentStash != nullptr ? ext : nullptr;
}

const Stash* ListHead(const PYPlayerData& ext, StashTabKind kind) {
    return kind == StashTabKind::Shared ? ext.sharedStash : ext.selfStash;
}

const Stash* FindPage(const PYPlayerData& ext, StashTabKind kind, uint32_t index) {
    if (index >= MAX_PAGES_WALKED) {
        return nullptr;
    }
    const Stash* page = ListHead(ext, kind);
    for (uint32_t i = 0; page != nullptr && i < index; ++i) {
        page = page->nextStash;
    }
    return page;
}

uint32_t PageCount(const PYPlayerData& ext, StashTabKind kind) {
    uint32_t count = 0;
    for (const Stash* page = ListHead(ext, kind); page != nullptr && count < MAX_PAGES_WALKED; page = page->nextStash) {
        ++count;
    }
    return count;
}

std::optional<PageRef> ActivePage(const PYPlayerData& ext) {
    for (const auto kind : {StashTabKind::Personal, StashTabKind::Shared}) {
        uint32_t index = 0;
        for (const Stash* page = ListHead(ext, kind); page != nullptr && index < MAX_PAGES_WALKED;
             page = page->nextStash, ++index) {
            if (page == ext.currentStash) {
                return PageRef{.kind = kind, .index = index};
            }
        }
    }
    return std::nullopt;
}

// Page names are typed into PlugY's in-game text box and stored in the ANSI code page.
std::string PageName(const Stash& page) {
    if (page.name == nullptr || page.name[0] == '\0') {
        return {};
    }
    return utils::ToStr(utils::ToWStr(page.name, CP_ACP));
}

// PlugY tracks no gold per page: personal pages share the character's stash
// gold, shared pages share one pool; both are attributed to page 0 of their kind.
uint32_t PageGold(const PYPlayerData& ext, StashTabKind kind, uint32_t index) {
    if (index != 0) {
        return 0;
    }
    if (kind == StashTabKind::Shared) {
        return ext.sharedGold;
    }
    const auto gold = Unit::Player().GetStat(STAT_GOLDBANK);
    return gold > 0 ? static_cast<uint32_t>(gold) : 0U;
}

StashTab ToTab(const PYPlayerData& ext, const Stash& page, StashTabKind kind, uint32_t index) {
    return {.kind = kind,
            .index = index,
            .type = StashTabType::Normal,
            .name = PageName(page),
            .isActive = &page == ext.currentStash,
            .gold = PageGold(ext, kind, index)};
}

void CollectTabs(const PYPlayerData& ext, StashTabKind kind, std::vector<StashTab>& out) {
    uint32_t index = 0;
    for (const Stash* page = ListHead(ext, kind); page != nullptr && index < MAX_PAGES_WALKED;
         page = page->nextStash, ++index) {
        out.push_back(ToTab(ext, *page, kind, index));
    }
}

bool IsStashItem(const D2UnitStrc* item) {
    return item->pItemData != nullptr &&
           static_cast<ItemLocation>(item->pItemData->pExtraData.nNodePos) == ItemLocation::Stash;
}

// The active page's items are the stash-located items of the player's inventory;
// an inactive page's items hang off its own list. Both chains are linked through
// the item's pNextItem, which is what INVENTORY_GetNextItem follows.
D2UnitStrc* FirstPageItem(const PYPlayerData& ext, const Stash& page) {
    if (&page != ext.currentStash) {
        return page.ptListItem;
    }
    auto* player = imports::d2client::UNITS_GetPlayerUnit();
    if (player == nullptr || player->pInventory == nullptr) {
        return nullptr;
    }
    return imports::d2common::INVENTORY_GetFirstItem(player->pInventory);
}

template <typename Fn>
void ForEachPageItem(const PYPlayerData& ext, const Stash& page, const Fn& fn) {
    const bool isActive = &page == ext.currentStash;
    for (auto* item = FirstPageItem(ext, page); item != nullptr;
         item = imports::d2common::INVENTORY_GetNextItem(item)) {
        if (isActive && !IsStashItem(item)) {
            continue;
        }
        fn(item);
    }
}

// The 0x3A commands that walk the server from `from` to `to`. PlugY only has
// relative moves, so a kind change lands on that kind's first page and the rest
// is singles (or a jump to the first page when that is shorter). The target must
// already exist in the client mirror: "next" past the last page creates a page
// server-side, and a script bug must not mint pages. Empty for an unknown target.
std::vector<uint8_t> PlanSwitch(const PYPlayerData& ext, PageRef from, PageRef to) {
    std::vector<uint8_t> commands;
    if (to.index >= PageCount(ext, to.kind)) {
        return commands;
    }
    PageRef cur = from;
    if (cur.kind != to.kind) {
        commands.push_back(to.kind == StashTabKind::Shared ? CMD_SELECT_SHARED : CMD_SELECT_PERSONAL);
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

void SendCommand(uint8_t command) {
    const std::array<uint8_t, 3> packet = {PACKET_SPEND_STAT_POINT, command, 0};
    SendGamePacket(packet);
}

// Polls the client mirror until `target` is the active page. Releases this
// thread's read locks while sleeping so the game thread can run the frames that
// deliver the server's page updates.
bool WaitForPage(PageRef target) {
    const GameReadLockReleaser releaser;
    const auto deadline = std::chrono::steady_clock::now() + PAGE_SWITCH_TIMEOUT;
    while (true) {
        {
            GameReadLock guard;
            const auto* ext = Pages();
            if (ext != nullptr && ActivePage(*ext) == target) {
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const profiling::ScopedSleep waiting;
        std::this_thread::sleep_for(PAGE_SWITCH_POLL);
    }
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

        const auto version = ReadVersion(module);
        Logger()->info("PlugY.dll {} is loaded; running its Init from the game's startup",
                       version ? Format(*version) : std::string("(unknown version)"));
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

// The client applies an item click locally and only then tells the server, and
// a page switch sent right behind the click can overtake it: the server would
// then park the page before it sees the pick-up, reject the pick-up, and leave
// the client holding an item the server still has in the stash. So a click is
// followed by a wait for the server's item action packet naming one of the
// items the click can touch (the cursor item and the target page's items).
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - single in-flight wait, guarded by the page mutex
std::mutex ackMutex;
std::vector<uint32_t> ackItemIds;
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
    const std::scoped_lock lock(ackMutex);
    if (std::ranges::find(ackItemIds, itemId) != ackItemIds.end()) {
        ackSeen.store(true, std::memory_order_release);
    }
}

// Ids the click on `page` can touch: whatever is on the cursor plus the page's items.
std::vector<uint32_t> ClickableItemIds(const PYPlayerData& ext, const Stash& page) {
    std::vector<uint32_t> ids;
    if (const auto cursor = Unit::CursorItem()) {
        ids.push_back(cursor->Id());
    }
    ForEachPageItem(ext, page, [&](const D2UnitStrc* item) { ids.push_back(item->dwUnitId); });
    return ids;
}

bool WaitForItemAck() {
    const GameReadLockReleaser releaser;
    const auto deadline = std::chrono::steady_clock::now() + ITEM_ACK_TIMEOUT;
    while (!ackSeen.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        const profiling::ScopedSleep waiting;
        std::this_thread::sleep_for(PAGE_SWITCH_POLL);
    }
    return true;
}

// Runs the click and, if it was handed to the game and could have moved
// something, blocks until the server has acknowledged the move.
ClickResult ClickAndAwaitAck(std::vector<uint32_t> itemIds, const std::function<ClickResult()>& action) {
    if (itemIds.empty()) {
        return action();
    }
    {
        const std::scoped_lock lock(ackMutex);
        ackItemIds = std::move(itemIds);
        ackSeen.store(false, std::memory_order_release);
    }
    hooks::intercepts::SetIncomingPacketObserver(&OnIncomingPacket);
    const ClickResult result = action();
    if (result == ClickResult::Dispatched && !WaitForItemAck()) {
        Logger()->warn("no server acknowledgement for the stash click within {} ms", ITEM_ACK_TIMEOUT.count());
    }
    hooks::intercepts::SetIncomingPacketObserver(nullptr);
    const std::scoped_lock lock(ackMutex);
    ackItemIds.clear();
    return result;
}

bool SwitchTo(PageRef target) {
    std::vector<uint8_t> plan;
    {
        GameReadLock guard;
        const auto* ext = Pages();
        if (ext == nullptr) {
            return false;
        }
        const auto from = ActivePage(*ext);
        if (!from) {
            return false;
        }
        if (*from == target) {
            return true;
        }
        plan = PlanSwitch(*ext, *from, target);
    }
    if (plan.empty()) {
        return false;
    }
    for (const auto command : plan) {
        SendCommand(command);
    }
    return WaitForPage(target);
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
    if (ReadByte(site) != OPCODE_CALL_INDIRECT[0] || ReadByte(site + 1) != OPCODE_CALL_INDIRECT[1]) {
        return;
    }
    const auto slot = ReadValue<uintptr_t>(site + 2);
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = kernel32 != nullptr ? GetProcAddress(kernel32, "LoadLibraryA") : nullptr;
    if (loadLibrary == nullptr || ReadValue<uintptr_t>(slot) != reinterpret_cast<uintptr_t>(loadLibrary)) {
        return;
    }
    static std::array<uint8_t, IAT_CALL_LEN> original{};
    hooks::WriteCallN(site, reinterpret_cast<uintptr_t>(&StartupLoadLibrary), IAT_CALL_LEN, original.data());
}

bool IsActive() {
    static std::mutex mutex;
    static std::optional<bool> detected;
    const std::scoped_lock lock(mutex);
    if (detected) {
        return *detected;
    }
    // Not cached: PlugY.dll is normally in the process before we are, but the
    // module lookup is cheap enough to repeat if it is not there yet.
    HMODULE module = GetModuleHandleW(L"PlugY.dll");
    if (module == nullptr) {
        return false;
    }
    detected = Detect(module);
    return *detected;
}

bool HasPages() {
    return Pages() != nullptr;
}

std::vector<StashTab> GetStashTabs() {
    std::vector<StashTab> tabs;
    const auto* ext = Pages();
    if (ext == nullptr) {
        return tabs;
    }
    CollectTabs(*ext, StashTabKind::Personal, tabs);
    CollectTabs(*ext, StashTabKind::Shared, tabs);
    return tabs;
}

std::vector<Unit> GetStashTabItems(StashTabKind kind, uint32_t index) {
    std::vector<Unit> items;
    const auto* ext = Pages();
    if (ext == nullptr) {
        return items;
    }
    const Stash* page = FindPage(*ext, kind, index);
    if (page == nullptr) {
        return items;
    }
    ForEachPageItem(*ext, *page, [&](D2UnitStrc* item) { items.push_back(Unit::FromPtr(item)); });
    return items;
}

std::optional<StashTab> FindStashTab(const Unit& item) {
    const auto* ext = Pages();
    if (ext == nullptr) {
        return std::nullopt;
    }
    const uint32_t itemId = item.Id();
    for (const auto kind : {StashTabKind::Personal, StashTabKind::Shared}) {
        uint32_t index = 0;
        for (const Stash* page = ListHead(*ext, kind); page != nullptr && index < MAX_PAGES_WALKED;
             page = page->nextStash, ++index) {
            bool found = false;
            ForEachPageItem(*ext, *page, [&](const D2UnitStrc* candidate) { found |= candidate->dwUnitId == itemId; });
            if (found) {
                return ToTab(*ext, *page, kind, index);
            }
        }
    }
    return std::nullopt;
}

ClickResult WithActivePage(StashTabKind kind, uint32_t index, const std::function<ClickResult()>& action) {
    // Drop this thread's read locks before queueing behind another script's page
    // operation: that operation needs the game thread, and the game thread needs
    // the write lock.
    const GameReadLockReleaser releaser;
    static std::mutex operationMutex;
    const std::scoped_lock lock(operationMutex);

    std::optional<PageRef> original;
    {
        GameReadLock guard;
        const auto* ext = Pages();
        if (ext != nullptr) {
            original = ActivePage(*ext);
        }
    }
    if (!original) {
        return ClickResult::StashTabUnavailable;
    }
    const PageRef target{.kind = kind, .index = index};
    if (target == *original) {
        return action();
    }
    if (!SwitchTo(target)) {
        Logger()->warn("stash page {}:{} could not be made active", static_cast<uint32_t>(kind), index);
        return ClickResult::StashTabUnavailable;
    }
    std::vector<uint32_t> itemIds;
    {
        GameReadLock guard;
        if (const auto* ext = Pages(); ext != nullptr) {
            if (const Stash* page = FindPage(*ext, kind, index); page != nullptr) {
                itemIds = ClickableItemIds(*ext, *page);
            }
        }
    }
    const ClickResult result = ClickAndAwaitAck(std::move(itemIds), action);
    if (!SwitchTo(*original)) {
        Logger()->warn("stash page {}:{} could not be restored after the click", static_cast<uint32_t>(original->kind),
                       original->index);
    }
    return result;
}

bool MoveSharedGold(GoldActionMode mode) {
    if (mode != GoldActionMode::Deposit && mode != GoldActionMode::Withdraw) {
        return false;
    }
    {
        GameReadLock guard;
        const auto* ext = Pages();
        if (ext == nullptr || ext->sharedStash == nullptr) {
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

ClickResult ClickParkedItem(ClickButton button, const Unit& item) {
    std::optional<StashTab> tab;
    {
        GameReadLock guard;
        tab = FindStashTab(item);
    }
    // An active-page item is a plain inventory click; refusing it here keeps
    // ClickItem's hand-off from looping back into this function.
    if (!tab || tab->isActive) {
        return ClickResult::InvalidTarget;
    }
    return WithActivePage(tab->kind, tab->index, [&] { return ClickItem(button, item); });
}

}  // namespace d2bs::game::plugy
