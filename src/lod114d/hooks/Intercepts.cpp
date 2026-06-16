// =============================================================================
// Inline-patch intercepts.
//
// IDA verification on 1.14d Game.exe (mod-base 0x400000), ports verbatim from
// reference/d2bs/D2Intercepts.cpp + D2Handlers.cpp + Patch.h. Each row's
// "site bytes" are the actual on-disk sequence we overwrite at install:
//
//   P1  0x47C89D  CALL/5    E8 0E BF FF FF                    OK  -> InputCall_I
//   P2  0x453B30  JMP/5     55 8B EC 83 EC                    OK  -> fn entry
//   P3  0x45F802  CALL/5    E8 19 C1 0C 00                    OK  -> ReceivePacket_I
//   P4  0x52AE5A  JMP/5     75 06 33 C0 5D                    OK  -> cond + epilog
//   P5  0x462D72  CALL/5    E8 99 4C 00 00                    OK  -> GetSelectedUnit
//   P8  0x44EBFB  byte/1    74                                OK  -> flip JZ->JNZ
//   P9  0x44EBEF  CALL/5    E8 9C 75 0A 00                    OK  -> CongratsScreen_I
//   P14 0x521B20  CALL/7    8B 55 08 8B CB FF D7              OK  -> ChatPacketRecv
//   P15 0x448813  CALL/7    81 EC 74 06 00 00 57              OK  -> Whisper_I
//   P16 0x442A61  CALL/5    E8 6A FE FF FF                    OK  -> ChannelInput_I
//   P17 0x4F9A0D  CALL/5    E8 5E FE FF FF                    OK  -> DrawSprites
//   P18 0x6091E5  CALL/10   89 51 10 C7 40 0C 00 00 00 00     OK  -> mid-function
//   P19 0x44B1AE  CALL/6    85 F6 74 02 FF D6                 OK  -> test+jz+call
//   P20 - not installed (reference's intercept logged before dispatching;
//        port's logger not wired, so calling MessageBoxA ourselves is a
//        no-op vs the unpatched site)
//   P21 0x4082E0  JMP/6     55 8B EC 6A FF 68                 OK  -> fn entry
//   P22 0x401790  JMP/6     55 8B EC B8 08 14                 OK  -> fn entry
//
// Conditional[] sites (installed only when their LaunchOptions toggle is set):
//
//   BypassMultiInstance    0x4F5623  CALL/6  FF 15 00 C5 6C 00     IAT FindWindowA
//   CreateWindowTitled     0x4F5831  CALL/6  FF 15 F0 C4 6C 00     IAT CreateWindowExA
//   BnetCache1             0x51944E  CALL/6  FF 15 14 C2 6C 00     IAT CreateFileA
//   BnetCache2             0x519434  CALL/6  FF 15 9C C1 6C 00     IAT CreateFileA
//   FailToJoinBackoff      0x44EF28  CALL/6  81 FE 30 75 00 00     cmp esi, 30000
//   ClassicCDKey           0x52366C  JMP/5   A3 44 27 88 00        mov [ClassicKey], eax
//   LodCDKey               0x523958  JMP/5   A3 4C 27 88 00        mov [XPacKey], eax
//
// All 15 installed sites verified; bytes match reference's expected shape.
// Patch P20 (no-op-without-replacement) and P6/P10/P11/P12/P13 explicitly
// skipped.
// =============================================================================

#include "hooks/Intercepts.h"

#include <Windows.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <system_error>

#include "game/GameCallbacks.h"
#include "game/GameHelpers.h"
#include "game/LaunchOptions.h"
#include "game/Types.h"
#include "hooks/HookManager.h"
#include "hooks/InlinePatch.h"
#include "imports/BnClient.h"
#include "imports/D2Client.h"
#include "imports/D2Game.h"
#include "imports/D2Gfx.h"
#include "imports/D2Launch.h"
#include "imports/D2Multi.h"
#include "imports/D2Net.h"
#include "imports/D2Win.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::hooks::intercepts {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - hook subsystem owns module-level state
uintptr_t moduleBase = 0;

// Tail-call targets for the naked thunks; populated by Init().
uintptr_t inputCall = 0;
uintptr_t receivePacket = 0;
uintptr_t sendPacketII = 0;
uintptr_t congratsScreen = 0;
uintptr_t channelInput = 0;
uintptr_t drawSprites = 0;
uintptr_t d2gameExit0 = 0;

// Tail-jump targets and snapshotted slot/string pointers for the
// Conditional[] CDKey intercepts. Snapshotted in Init() so the naked
// thunks can reference them as `dword ptr [g_*]` without CRT calls.
uintptr_t bnclientDClass = 0;
uintptr_t bnclientDLod = 0;
char** bnclientClassicKeySlot = nullptr;
char** bnclientXPacKeySlot = nullptr;
const char* classicCdKey = nullptr;
const char* lodCdKey = nullptr;

// Per-site original-byte storage. Sized for the longest patch (10 bytes for
// P18); shorter ones leave the trailing slots untouched.
struct SiteState {
    bool installed = false;
    std::array<uint8_t, 10> original{};
};

SiteState siteP1, siteP2, siteP3, siteP4, siteP5;
SiteState siteP8, siteP9, siteP14, siteP15, siteP16;
SiteState siteP17, siteP18, siteP19, siteP21, siteP22;

// Conditional[] sites - installed only when their LaunchOptions toggle is set.
SiteState siteBypassMultiInstance, siteCreateWindowTitled;
SiteState siteBnetCache1, siteBnetCache2;
SiteState siteClassicCdKey, siteLodCdKey, siteFailToJoinBackoff;

// P5 click-target override. clickActionActive mirrors reference Vars.bClickAction; null with active=true forces "no
// unit selected" instead of game hover selection.
std::atomic<bool> clickActionActive{false};
std::atomic<D2UnitStrc*> clickActionUnit{nullptr};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

constexpr uint32_t P1_RVA = 0x7C89D;
constexpr uint32_t P2_RVA = 0x53B30;
constexpr uint32_t P3_RVA = 0x5F802;
constexpr uint32_t P4_RVA = 0x12AE5A;
constexpr uint32_t P5_RVA = 0x62D72;
constexpr uint32_t P8_RVA = 0x4EBFB;
constexpr uint32_t P9_RVA = 0x4EBEF;
constexpr uint32_t P14_RVA = 0x121B20;
constexpr uint32_t P15_RVA = 0x48813;
constexpr uint32_t P16_RVA = 0x42A61;
constexpr uint32_t P17_RVA = 0xF9A0D;
constexpr uint32_t P18_RVA = 0x2091E5;
constexpr uint32_t P19_RVA = 0x4B1AE;
constexpr uint32_t P21_RVA = 0x82E0;
constexpr uint32_t P22_RVA = 0x1790;

// Conditional[] sites. CALL sites are 6 bytes (original `FF 15 imm32` IAT
// indirect calls / `81 FE imm32` CMP); we replace each with a 5-byte
// `E8 rel32` plus one trailing NOP. JMP sites are 5 bytes (original
// `A3 imm32` MOV [imm32], EAX); we overwrite with `E9 rel32`.
constexpr uint32_t BYPASS_MULTI_INSTANCE_RVA = 0xF5623;
constexpr uint32_t CREATE_WINDOW_TITLED_RVA = 0xF5831;
constexpr uint32_t BNET_CACHE_1_RVA = 0x11944E;
constexpr uint32_t BNET_CACHE_2_RVA = 0x119434;
constexpr uint32_t CLASSIC_CDKEY_RVA = 0x12366C;
constexpr uint32_t LOD_CDKEY_RVA = 0x123958;
constexpr uint32_t FAIL_TO_JOIN_BACKOFF_RVA = 0x4EF28;

// =============================================================================
// C-side callback dispatchers
// =============================================================================
// C dispatchers for naked thunks. Must not throw (inside pushad/popad frame); onX callbacks are fire-and-forget and
// handle their own exceptions.

// P1: GameInput. wMsg arrives in ecx (fastcall via `mov ecx, ebx`). Returns
// `-1` to block the chat from reaching the game; `0` to pass through.
extern "C" uint32_t __fastcall OnGameInput(wchar_t* wMsg) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || cb->onChatInput == nullptr || wMsg == nullptr) {
        return 0;
    }
    return cb->onChatInput(utils::ToStr(std::wstring{wMsg})) ? static_cast<uint32_t>(-1) : 0U;
}

// P3: GamePacketReceived. Returns non-zero to allow the packet through, 0 to
// block. Reference dispatched per-packet-id handlers in C; we do the same and
// fan out via the `onChatMessage`, `onItemAction`, `onGameEvent` callbacks.
//
// Block-decision policy: if EITHER the global `onGamePacketReceived` handler
// OR a blockable per-id handler (`onChatMessage`) returns true, the packet
// is blocked. Reference's equivalent was `!event && perHandler` (pass-through
// requires both no-event AND per-handler-allows). Void per-id handlers
// (`onGameEvent`, `onItemAction`) have no block authority.
extern "C" uint32_t __fastcall OnGamePacketReceived(uint8_t* packet, uint32_t size) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || packet == nullptr || size == 0) {
        return 1;  // pass through
    }

    // Warden detection - reference TerminateProcess(self) on 0xAE; the new
    // framework has no opinion, so we just block and let upper layers decide.
    if (packet[0] == 0xAE) {
        TerminateProcess(GetCurrentProcess(), 0);
        return 0;
    }

    bool blocked = false;
    if (cb->onGamePacketReceived != nullptr) {
        blocked = cb->onGamePacketReceived(std::span<const uint8_t>{packet, size});
    }

    switch (packet[0]) {
        case 0x26: {  // chat
            if (size >= 12 && cb->onChatMessage != nullptr) {
                const auto* who = reinterpret_cast<const char*>(packet) + 10;
                const size_t whoLen = strnlen(who, size - 10);
                if (10U + whoLen + 1U < size) {
                    const auto* msg = who + whoLen + 1;
                    const size_t maxLen = size - (10U + whoLen + 1U);
                    if (cb->onChatMessage(std::string{who, whoLen}, std::string{msg, strnlen(msg, maxLen)})) {
                        blocked = true;
                    }
                }
            }
            break;
        }
        case 0x5A: {  // game event
            if (size >= 28 && cb->onGameEvent != nullptr) {
                const auto mode = static_cast<int32_t>(packet[1]);
                uint32_t param1 = 0;
                std::memcpy(&param1, packet + 3, sizeof(uint32_t));
                const auto param2 = static_cast<uint32_t>(packet[7]);
                const auto* name1 = reinterpret_cast<const char*>(packet) + 8;
                const auto* name2 = reinterpret_cast<const char*>(packet) + 24;
                const size_t name1Max = std::min<size_t>(16, size - 8);
                const size_t name2Max = std::min<size_t>(16, size > 24 ? size - 24 : 0);
                cb->onGameEvent(mode, param1, param2, std::string{name1, strnlen(name1, name1Max)},
                                std::string{name2, strnlen(name2, name2Max)});
            }
            break;
        }
        case 0x9C:
        case 0x9D: {  // item action
            if (size >= 19 && cb->onItemAction != nullptr) {
                const uint8_t mode = packet[1];
                uint32_t gid = 0;
                std::memcpy(&gid, packet + 4, sizeof(uint32_t));
                const uint8_t dest = (packet[13] & 0x1C) >> 2;
                int64_t icode = 0;
                switch (dest) {
                    case 0:
                    case 2:
                        std::memcpy(&icode, packet + 15, sizeof(int64_t));
                        icode >>= 0x04;
                        break;
                    case 3:
                    case 4:
                    case 6:
                        if ((mode == 0 || mode == 2) && dest == 3) {
                            std::memcpy(&icode, packet + 17, sizeof(int64_t));
                            icode >>= 0x05;
                        } else if (mode != 0xF && mode != 1 && mode != 12) {
                            std::memcpy(&icode, packet + 17, sizeof(int64_t));
                            icode >>= 0x1C;
                        } else {
                            std::memcpy(&icode, packet + 15, sizeof(int64_t));
                            icode >>= 0x04;
                        }
                        break;
                    default:
                        break;
                }
                // 4-byte D2 item code (e.g. "rin ", "amu ", "ssd ") plus a trailing
                // NUL so std::string{code.data()} stops on the C-string terminator.
                // Matches reference/d2bs/D2NetHandlers.cpp:72,135 (`char code[5] = "";`).
                std::array<char, 5> code{};
                std::memcpy(code.data(), &icode, 4);
                if (code[3] == ' ') {
                    code[3] = '\0';
                }
                cb->onItemAction(gid, mode, std::string{code.data()}, packet[0] == 0x9D);
            }
            break;
        }
        case 0x69: {  // monster mode update
            // The action byte (offset 0x05) is the client's mode code, NOT the
            // MONMODE enum: the game's internal table maps MONMODE_DEATH -> 8 (a
            // kill happening now) and MONMODE_DEAD -> 9 (a corpse that was already
            // dead when it streamed into view). Count 8 only - 9 would double-count
            // pre-existing corpses. unitId (offset 0x01) is still resolvable here,
            // so the framework can read the dying monster's class / super-unique id.
            constexpr uint8_t MONSTER_DEATH_ACTION = 8;
            if (size >= 0x0C && cb->onMonsterDeath != nullptr && packet[5] == MONSTER_DEATH_ACTION) {
                uint32_t unitId = 0;
                std::memcpy(&unitId, packet + 1, sizeof(uint32_t));
                cb->onMonsterDeath(unitId);
            }
            break;
        }
        default:
            break;
    }
    return blocked ? 0U : 1U;
}

// P4: GamePacketSent. ecx=packet, edx=size (the naked thunk pulls them off
// the saved context - see the asm body). Returns non-zero to allow.
extern "C" uint32_t __fastcall OnGamePacketSent(uint8_t* packet, uint32_t size) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || cb->onGamePacketSent == nullptr || packet == nullptr || size == 0) {
        return 1;
    }
    return cb->onGamePacketSent(std::span<const uint8_t>{packet, size}) ? 0 : 1;
}

// P2 / P17: GameDraw / GameDrawOOG. Plain `void` calls - the framework
// flushes drawables and runs Console::Draw inside onDraw.
extern "C" void OnGameDraw() {
    const auto* cb = GetActiveCallbacks();
    if (cb != nullptr && cb->onDraw != nullptr) {
        cb->onDraw();
    }
}

// P14: ChatPacketRecv (BNet). pPacket in ecx (after `sub ecx,4` in the
// thunk -> original buffer base), arg in edx (after `add edx,4` -> length-ish
// per reference). Returns non-zero to pass through.
extern "C" uint32_t __fastcall OnChatPacketRecv(const uint8_t* packet, uint32_t /*len*/) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || packet == nullptr) {
        return 1;
    }
    if (packet[1] != 0x0F) {
        return 1;  // not a chat-format SID - pass through
    }
    const uint8_t mode = packet[4];
    const auto* who = reinterpret_cast<const char*>(packet) + 28;
    const size_t whoLen = strnlen(who, 64);
    const auto* said = who + whoLen + 1;
    const size_t saidLen = strnlen(said, 256);
    switch (mode) {
        case 0x02:
            if (cb->onChatMessage != nullptr) {
                cb->onChatMessage(std::string{who, whoLen}, "joined the channel");
            }
            break;
        case 0x03:
            if (cb->onChatMessage != nullptr) {
                cb->onChatMessage(std::string{who, whoLen}, "left the channel");
            }
            break;
        case 0x04:
        case 0x0A:
            if (cb->onWhisper != nullptr) {
                cb->onWhisper(std::string{who, whoLen}, std::string{said, saidLen});
            }
            break;
        case 0x05:
        case 0x12:
        case 0x13:
        case 0x17:
            if (cb->onChatMessage != nullptr) {
                cb->onChatMessage(std::string{who, whoLen}, std::string{said, saidLen});
            }
            break;
        default:
            break;
    }
    return 1;
}

// P15: Whisper (OOG). ecx = sender (char*), edx = text (char*).
extern "C" void __fastcall OnWhisperHandler(const char* szAcc, const char* szText) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || cb->onWhisper == nullptr || szAcc == nullptr || szText == nullptr) {
        return;
    }
    cb->onWhisper(std::string{szAcc, strnlen(szAcc, 64)}, std::string{szText, strnlen(szText, 256)});
}

// P16: ChannelInput. wMsg arrives in ecx. Returns non-zero to pass through;
// 0 to block (matches reference's `test eax, eax; jz SkipInput`).
extern "C" uint32_t __fastcall OnChannelInput(wchar_t* wMsg) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || cb->onChatInput == nullptr || wMsg == nullptr) {
        return 1;
    }
    return cb->onChatInput(utils::ToStr(std::wstring{wMsg})) ? 0U : 1U;
}

// P19: RealmPacketRecv. Returns non-zero to pass through.
extern "C" uint32_t __fastcall OnRealmPacketRecv(uint8_t* packet, uint32_t size) {
    const auto* cb = GetActiveCallbacks();
    if (cb == nullptr || cb->onRealmPacket == nullptr || packet == nullptr || size == 0) {
        return 1;
    }
    return cb->onRealmPacket(std::span<const uint8_t>{packet, size}) ? 0U : 1U;
}

// === Conditional[] intercepts (LaunchOptions-gated) =========================

// Replaces D2Gfx's FindWindowA call that gates against existing D2
// processes. Always returning nullptr signals "no existing window" and
// lets multiple D2 instances coexist.
extern "C" HWND __stdcall BypassMultiInstanceCheck(LPCSTR /*className*/, LPCSTR /*windowName*/) {
    return nullptr;
}

// Replaces D2Gfx's CreateWindowExA call so each instance gets a distinct
// title (selected via -title). Falls back to "Diablo II" when the launcher
// didn't pass a title.
extern "C" HWND __stdcall CreateGameWindowWithTitle(DWORD exStyle, LPCSTR className, LPCSTR /*windowName*/, DWORD style,
                                                    int32_t x, int32_t y, int32_t width, int32_t height, HWND parent,
                                                    HMENU menu, HINSTANCE instance, LPVOID param) {
    const auto& opts = game::GetLaunchOptions();
    const wchar_t* title = opts.windowTitle.empty() ? L"Diablo II" : opts.windowTitle.c_str();
    const std::wstring wideClass = d2bs::utils::ToWStr(className == nullptr ? "" : className, CP_ACP);
    return ::CreateWindowExW(exStyle, wideClass.c_str(), title, style, x, y, width, height, parent, menu, instance,
                             param);
}

// Best-effort cleanup of stale bncache*.dat files from cwd. Mirrors
// reference's EraseCacheFiles helper. Errors are silently swallowed -
// OpenPerInstanceBnetCache picks a unique filename anyway.
void EraseBnetCacheFiles() {
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator{cwd, ec}) {
        if (ec) {
            return;
        }
        const auto name = entry.path().filename().string();
        if (name.starts_with("bncache") && name.ends_with(".dat")) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

// Reads the game window's current title, returning an empty string if no
// window exists yet or the call fails. Used to suffix per-instance cache
// filenames so two clients with distinct -title launches don't collide.
std::string ReadWindowTitle() {
    HWND hwnd = imports::d2gfx::WINDOW_GetWindow();
    if (hwnd == nullptr) {
        return {};
    }
    std::array<char, 128> buffer{};
    const int len = ::GetWindowTextA(hwnd, buffer.data(), static_cast<int>(buffer.size()));
    if (len <= 0) {
        return {};
    }
    return std::string{buffer.data(), static_cast<size_t>(len)};
}

// Replaces BNClient's CreateFileA call that opens bncache.dat. Each
// instance writes to a per-window-title (or randomised) file so two
// concurrent clients don't race on the same path.
extern "C" HANDLE __stdcall OpenPerInstanceBnetCache(LPCSTR /*fileName*/, DWORD desiredAccess, DWORD shareMode,
                                                     LPSECURITY_ATTRIBUTES securityAttrs, DWORD creationDisposition,
                                                     DWORD flagsAndAttributes, HANDLE templateFile) {
    EraseBnetCacheFiles();

    std::error_code ec;
    auto path = std::filesystem::current_path(ec);
    const std::string title = ReadWindowTitle();
    if (title.size() > 1) {
        path /= std::format("bncache{}.dat", title);
    } else {
        // Coarse uniqueness suffix - collisions just trigger another
        // EraseBnetCacheFiles on the next launch.
        const auto tick =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        path /= std::format("bncache{}.dat", static_cast<uint32_t>(tick) % 0x2000U);
    }

    return ::CreateFileA(path.string().c_str(), desiredAccess, shareMode, securityAttrs, creationDisposition,
                         flagsAndAttributes, templateFile);
}

// P22: ErrorReportLaunch - D2's own crash path. Called when D2 has decided
// it's about to terminate (typically after its own __try/__except caught
// something our SetUnhandledExceptionFilter never sees). This is our most
// reliable point to capture diagnostics: the original behaviour was to call
// ExitProcess directly to suppress the crash-report UI, but exiting silently
// hides the cause. Log everything we can, dump a crash log next to Game.exe,
// then exit.
extern "C" char __fastcall OnErrorReportLaunch(const char* crashFile, int /*a2*/) {
    // Sentinel write - bypasses spdlog/console. Visible in DebugView even if
    // the logging pipeline is wedged.
    OutputDebugStringA("\n*** d2bsng OnErrorReportLaunch entered ***\n");

    auto desc = d2bs::thread_utils::GetThreadDescription(0);
    auto stack = d2bs::thread_utils::GetThreadStacktrace(0, 0);
    auto dump = std::format("D2 ErrorReportLaunch fired (D2 is crashing)\n"
                            "crash file: {}\n"
                            "thread id: {:#x}\n"
                            "{}"
                            "stack:\n{}\n",
                            crashFile != nullptr ? crashFile : "<null>", GetCurrentThreadId(),
                            desc.empty() ? "" : std::format("thread description: {}\n", desc), stack);
    d2bs::thread_utils::CrashAndExit(dump, 0xD2B50003);
}

// =============================================================================
// Naked-asm thunks. MSVC requires __declspec(naked) functions to use only inline __asm. C dispatchers are extern "C" so
// name mangling does not break the asm call resolution.
// =============================================================================

// P1: GameInput intercept. Original: `CALL D2CLIENT_InputCall_I` at 0x47C89D.
// On entry: ebx holds wMsg (`mov ecx, ebx; call OnGameInput`).
extern "C" __declspec(naked) void GameInputThunk() {
    __asm {
        pushad
        mov ecx, ebx
        call OnGameInput
        cmp eax, -1
        popad
        je BlockIt
        jmp dword ptr [inputCall]
BlockIt:
        xor eax, eax
        ret
    }
}

// P2: GameDraw. Runs the onDraw callback and returns; the replaced function's original draw work fires separately. Kept
// naked for uniformity with the other patch install paths.
extern "C" __declspec(naked) void GameDrawThunk() {
    __asm {
        pushad
        call OnGameDraw
        popad
        ret
    }
}

// P3: GamePacketReceived. Original: `CALL D2NET_ReceivePacket_I` at
// 0x45F802. The original CALL site has ecx=pPacket, edx=dwSize set up by the
// caller; pushad preserves them, so OnGamePacketReceived (declared
// __fastcall) gets them straight through. After popad we replicate the
// original CALL by re-pushing arg3 from the stack and jumping to
// D2NET_ReceivePacket_I; a `ret 4` after that returns + cleans the arg3.
extern "C" __declspec(naked) void GamePacketReceivedThunk() {
    __asm {
        pushad
        call OnGamePacketReceived
        test eax, eax
        popad
        jnz OldCode
        mov edx, 0
OldCode:
        mov eax, [esp + 4]
        push eax
        call dword ptr [receivePacket]
        ret 4
    }
}

// P4: GamePacketSent. Original: `JMP/5` overwrites the byte sequence
// `75 06 33 C0 5D` at 0x52AE5A (post-prologue inside D2NET_SendPacket).
// Stack at jmp: [esp+0]=savedEBP, [esp+4]=ret, [esp+8]=aLen, [esp+0xC]=arg1,
// [esp+0x10]=aPacket. We load ecx=aPacket, edx=aLen for fastcall
// `OnGamePacketSent(pPacket, dwSize)`. On block: zero out aLen so the
// epilogue's send is a no-op. Allow tail-target = D2CLIENT_SendPacket_II
// at 0x52AE62 (= original site + 8). Block tail-target = the bytes we
// overwrote: `xor eax, eax; pop ebp; ret 0Ch`.
//
// Stack-frame size constant: pushfd (4) + pushad (32) = 0x24 bytes.
// Reference uses 0x22 (pushfw assumption); MSVC inline `pushfd` is the
// portable way to push EFLAGS in 32-bit mode and wins over the ambiguity.
extern "C" __declspec(naked) void GamePacketSentThunk() {
    __asm {
        pushfd
        pushad
            // [esp+0x24] = saved ebp; +4 = retaddr; +8 = aLen; +0xC = arg1; +0x10 = aPacket
        mov ecx, [esp + 0x24 + 0x10]  // ecx = aPacket
        mov edx, [esp + 0x24 + 0x8]  // edx = aLen
        call OnGamePacketSent
        test eax, eax
        popad
        jnz OldCode
            // post-popad: [esp+0]=pushfd-eflags(4), +4=ebp, +8=retaddr, +0xC=aLen
        mov dword ptr [esp + 0xC], 0               // zero aLen on block
OldCode:
        popfd
                        // popfd restores caller's flags from before our pushfd - at the patch
                        // site, those flags were the result of `cmp dword_882B34, 0`. The
                        // original `jnz +6` at the site went to D2CLIENT_SendPacket_II if
                        // dword_882B34 != 0, else fell through to xor/pop/ret. We replicate.
        jnz Good
        xor eax, eax
        pop ebp
        ret 0Ch
Good:
        jmp dword ptr [sendPacketII]
    }
}

// P5: GetSelectedUnit - patches the CALL inside D2CLIENT_ClickMap that
// resolves the click target. The original (sub_467A10) reads from D2's
// SelectedUnit state but then runs a mouse-hover validation
// (sub_466870(0,0)) that fails when MouseX/Y is forced to 0,0 - which is
// exactly what we do for script-driven clicks, so the original would
// always return NULL and ClickMap would treat the click as "walk to
// empty space" instead of interacting with the targeted unit. The
// intercept short-circuits to a script-set unit when one is staged.
// Reference: reference/d2bs/Patch.h:13 + D2Intercepts.cpp:92-104.
extern "C" D2UnitStrc* OnGetSelectedUnit() {
    if (clickActionActive.load(std::memory_order_acquire)) {
        return clickActionUnit.load(std::memory_order_acquire);
    }
    return imports::d2client::UNITS_GetSelectedUnit();
}

// P9: CongratsScreen. Original CALL/5 at 0x44EBEF -> CongratsScreen_I.
// Bumps `nMaxDiff` to 10 (any value high enough to enable the next
// difficulty selector) after the Baal credits roll on any non-Hell
// difficulty, so Nightmare unlocks after Normal-Baal and Hell unlocks
// after Nightmare-Baal.
//
// Reference d2bs (D2Handlers.cpp:436) fired this only on Nightmare:
//     if (D2CLIENT_GetDifficulty() == 1) { ... nMaxDiff = 10; }
// We believe that was a bug - Normal -> Nightmare needs the same unlock
// and there's no documented reason to skip it. We widen the gate to
// `< DIFFMODE_HELL`. The expansion-class gate stays (Hell is LoD-only).
// If the game-side progression flag turns out to handle the Normal ->
// Nightmare transition itself, the symptom is benign (we'd be writing
// the same value the game already wrote).
extern "C" void SetMaxDiff() {
    if (game::GetDifficulty() < game::Difficulty::Hell && *imports::d2client::gbExpCharFlag != 0) {
        auto* pData = *imports::d2launch::gpBnetData;
        if (pData != nullptr) {
            pData->nMaxDiff = 10;
        }
    }
}

extern "C" __declspec(naked) void CongratsScreenThunk() {
    __asm {
        call dword ptr [congratsScreen]
        pushad
        call SetMaxDiff
        popad
        ret
    }
}

// P14: ChatPacketRecv. Original CALL/7. Reference computes `ecx = ebx-4,
// edx = [ebp+8]+4`, dispatches, then conditionally `call edi`.
extern "C" __declspec(naked) void ChatPacketRecvThunk() {
    __asm {
        mov edx, [ebp + 8]
        mov ecx, ebx
        pushad
        sub ecx, 4
        add edx, 4
        // C handler signature: (uint8_t* packet, uint32_t len). Win32 fastcall
        // wants ecx=packet, edx=len. We already have those after the sub/add.
        call OnChatPacketRecv
        test eax, eax
        popad
        je Block
        call edi
Block:
        ret
    }
}

// P15: Whisper (OOG). Original CALL/7 -> bytes `81 EC 74 06 00 00 57`
// (sub esp,674h; push edi). Reference re-saves edi, opens the stack frame,
// calls the C handler with `ecx = ebx, edx = [ebp+8]`, then jmps to edi (the
// original return target stashed before sub esp).
extern "C" __declspec(naked) void WhisperThunk() {
    __asm {
        mov [esp - 0x674 - 4 + 4], edi
        pop edi
        sub esp, 0x678  // matches reference D2Intercepts.cpp:111 (originally 0x674 sub + push edi = 0x678 net)
        pushad
        mov ecx, ebx
        mov edx, [ebp + 8]
        call OnWhisperHandler
        popad
        jmp edi
    }
}

// P16: ChannelInput. Original CALL/5 -> ChannelInput_I.
extern "C" __declspec(naked) void ChannelInputThunk() {
    __asm {
        push ecx
        mov ecx, esi
        call OnChannelInput
        test eax, eax
        pop ecx
        jz SkipInput
        jmp dword ptr [channelInput]
SkipInput:
        ret
    }
}

// P17: GameDrawOOG. Original CALL/5 at 0x4F9A0D -> D2WIN_DrawSprites
// (sub_4F9870). Unlike P2 (which replaces a function entry), P17 replaces a
// CALL inside a caller; the original called D2WIN_DrawSprites. We must run
// the original AND the d2bs onDraw, in that order (matches reference's
// `GameDrawOOG_Intercept` which is a C function that calls DrawSprites first
// then dispatches the d2bs draw work).
extern "C" __declspec(naked) void GameDrawOOGThunk() {
    __asm {
        pushad
        call dword ptr [drawSprites]
        call OnGameDraw
        popad
        ret
    }
}

// P18: GameCrashFix. Original 10 bytes at 0x6091E5:
//   89 51 10                  mov [ecx+0x10], edx
//   C7 40 0C 00 00 00 00      mov [eax+0xC], 0
// The fix guards the first store with `test ecx, ecx`. Verbatim from
// reference; no callback.
extern "C" __declspec(naked) void GameCrashFixThunk() {
    __asm {
        cmp ecx, 0
        je Skip
        mov dword ptr [ecx + 0x10], edx
Skip:
        mov dword ptr [eax + 0xC], 0
        ret
    }
}

// P19: RealmPacketRecv. Original CALL/6: `test esi,esi; jz +2; call esi`.
// Reference: dispatch C, if returns 0 ret immediately, else call esi.
extern "C" __declspec(naked) void RealmPacketRecvThunk() {
    __asm {
        test esi, esi
        jz Block
        pushad
        call OnRealmPacketRecv
        cmp eax, 0
        popad
        je Block
        call esi
Block:
        ret
    }
}

// P21: D2GAME_exit0 redirect. Original is the function entry of a Fog
// fatal-error helper at 0x4082E0. We redirect to D2GAME's exit0 at 0x40576F.
extern "C" __declspec(naked) void D2GAMEExit0Thunk() {
    __asm {
        jmp dword ptr [d2gameExit0]
    }
}

// === Conditional[] naked thunks =============================================

// Replaces the FTJ backoff check (`cmp esi, 0x7530` at 0x44EF28) with a
// constant `cmp esi, 4000` so the downstream JBE branches earlier. The
// 6-byte patch site is reduced to a 5-byte CALL + 1 NOP; the intercept
// does its own CMP and returns so EFLAGS feed the original JBE.
extern "C" __declspec(naked) void BypassFailToJoinBackoffThunk() {
    __asm {
        cmp esi, 4000
        ret
    }
}

// Replaces the `MOV [ClassicKey], EAX` at 0x52366C (followed by a 2-byte
// short JMP that bypasses an alternative path). We write the launcher-
// supplied key into the slot and tail-jump into the otherwise-skipped
// alternative path at 0x523673 - both paths reconverge at 0x523678.
extern "C" __declspec(naked) void InjectClassicCdKeyThunk() {
    __asm {
        mov eax, dword ptr [classicCdKey]
        mov ecx, dword ptr [bnclientClassicKeySlot]
        mov [ecx], eax
        jmp dword ptr [bnclientDClass]
    }
}

// Replaces the `MOV [XPacKey], EAX` at 0x523958 with the same pattern as
// InjectClassicCdKeyThunk but for the LoD slot. Tail-jumps to the
// sequential continuation at 0x52395D.
extern "C" __declspec(naked) void InjectLodCdKeyThunk() {
    __asm {
        mov eax, dword ptr [lodCdKey]
        mov ecx, dword ptr [bnclientXPacKeySlot]
        mov [ecx], eax
        jmp dword ptr [bnclientDLod]
    }
}

// =============================================================================
// Install / Remove
// =============================================================================

void InstallSite(SiteState& s, uint32_t rva, void (*thunk)(), size_t len, bool isJmp) {
    if (s.installed) {
        return;
    }
    const uintptr_t site = moduleBase + rva;
    const uintptr_t target = reinterpret_cast<uintptr_t>(thunk);
    if (isJmp) {
        WriteJmpN(site, target, len, s.original.data());
    } else {
        WriteCallN(site, target, len, s.original.data());
    }
    s.installed = true;
}

void RestoreSite(SiteState& s, uint32_t rva, size_t len) {
    if (!s.installed) {
        return;
    }
    RestoreN(moduleBase + rva, s.original.data(), len);
    s.installed = false;
}

}  // namespace

// =============================================================================
// Public API
// =============================================================================

namespace {

void PopInitFailure(const wchar_t* detail) {
    std::wstring msg = L"d2bsng: failed to initialise inline-patch intercepts - ";
    msg += detail;
    MessageBoxW(nullptr, msg.c_str(), L"d2bsng init failure", MB_OK | MB_ICONERROR);
}

}  // namespace

bool Init() {
    moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (moduleBase == 0) {
        PopInitFailure(L"GetModuleHandle returned NULL");
        return false;
    }

    // Each entry pairs (display name, IsResolved-style probe, snapshot action).
    // Probes are evaluated lazily so we can short-circuit at the first failure
    // and report exactly which import was missing.
    struct Probe {
        const wchar_t* name;
        bool resolved;
    };
    const std::array<Probe, 11> probes = {{
        {.name = L"D2CLIENT InputCall_I", .resolved = imports::d2client::InputCall_I.IsResolved()},
        {.name = L"D2NET ReceivePacket_I", .resolved = imports::d2net::CLIENT_ReceivePacket_I.IsResolved()},
        {.name = L"D2CLIENT SendPacket_II", .resolved = imports::d2client::SendPacket_II.IsResolved()},
        {.name = L"D2CLIENT CongratsScreen_I", .resolved = imports::d2client::CongratsScreen_I.IsResolved()},
        {.name = L"D2MULTI ChannelInput_I", .resolved = imports::d2multi::ChannelInput_I.IsResolved()},
        {.name = L"D2WIN DrawSprites", .resolved = imports::d2win::D2WIN_DrawSprites.IsResolved()},
        {.name = L"D2GAME Exit", .resolved = imports::d2game::D2GAME_Exit.IsResolved()},
        {.name = L"BNCLIENT DClass", .resolved = imports::bnclient::DClass.IsResolved()},
        {.name = L"BNCLIENT DLod", .resolved = imports::bnclient::DLod.IsResolved()},
        {.name = L"BNCLIENT ClassicKey", .resolved = imports::bnclient::gpszClassicCdKey.IsResolved()},
        {.name = L"BNCLIENT XPacKey", .resolved = imports::bnclient::gpszExpansionCdKey.IsResolved()},
    }};
    for (const auto& p : probes) {
        if (!p.resolved) {
            std::wstring detail = L"unresolved import: ";
            detail += p.name;
            PopInitFailure(detail.c_str());
            return false;
        }
    }

    inputCall = imports::d2client::InputCall_I.Addr();
    receivePacket = reinterpret_cast<uintptr_t>(imports::d2net::CLIENT_ReceivePacket_I.Ptr());
    sendPacketII = imports::d2client::SendPacket_II.Addr();
    congratsScreen = imports::d2client::CongratsScreen_I.Addr();
    channelInput = imports::d2multi::ChannelInput_I.Addr();
    drawSprites = reinterpret_cast<uintptr_t>(imports::d2win::D2WIN_DrawSprites.Ptr());
    d2gameExit0 = reinterpret_cast<uintptr_t>(imports::d2game::D2GAME_Exit.Ptr());

    bnclientDClass = imports::bnclient::DClass.Addr();
    bnclientDLod = imports::bnclient::DLod.Addr();
    bnclientClassicKeySlot = imports::bnclient::gpszClassicCdKey.Ptr();
    bnclientXPacKeySlot = imports::bnclient::gpszExpansionCdKey.Ptr();

    const auto& opts = game::GetLaunchOptions();
    classicCdKey = opts.classicCdKey.empty() ? nullptr : opts.classicCdKey.c_str();
    lodCdKey = opts.lodCdKey.empty() ? nullptr : opts.lodCdKey.c_str();
    return true;
}

void InstallAll() {
    if (moduleBase == 0) {
        return;  // Init() not yet run; bail gracefully.
    }

    InstallSite(siteP1, P1_RVA, &GameInputThunk, 5, /*isJmp=*/false);
    InstallSite(siteP2, P2_RVA, &GameDrawThunk, 5, /*isJmp=*/true);
    InstallSite(siteP3, P3_RVA, &GamePacketReceivedThunk, 5, /*isJmp=*/false);
    InstallSite(siteP4, P4_RVA, &GamePacketSentThunk, 5, /*isJmp=*/true);
    InstallSite(siteP5, P5_RVA, reinterpret_cast<void (*)()>(&OnGetSelectedUnit), 5, /*isJmp=*/false);

    // P8 byte flip: 0x74 (JZ) -> 0x75 (JNZ). Pairs with P9.
    {
        uint8_t saved = 0;
        if (!siteP8.installed) {
            WriteByte(moduleBase + P8_RVA, 0x75, saved);
            siteP8.original[0] = saved;
            siteP8.installed = true;
        }
    }

    InstallSite(siteP9, P9_RVA, &CongratsScreenThunk, 5, /*isJmp=*/false);
    InstallSite(siteP14, P14_RVA, &ChatPacketRecvThunk, 7, /*isJmp=*/false);
    InstallSite(siteP15, P15_RVA, &WhisperThunk, 7, /*isJmp=*/false);
    InstallSite(siteP16, P16_RVA, &ChannelInputThunk, 5, /*isJmp=*/false);
    InstallSite(siteP17, P17_RVA, &GameDrawOOGThunk, 5, /*isJmp=*/false);
    InstallSite(siteP18, P18_RVA, &GameCrashFixThunk, 10, /*isJmp=*/false);
    InstallSite(siteP19, P19_RVA, &RealmPacketRecvThunk, 6, /*isJmp=*/false);
    // P20 (LogMessageBoxA) skipped - see comment above the would-be thunk.
    InstallSite(siteP21, P21_RVA, &D2GAMEExit0Thunk, 6, /*isJmp=*/true);
    InstallSite(siteP22, P22_RVA, reinterpret_cast<void (*)()>(&OnErrorReportLaunch), 6, /*isJmp=*/true);

    // Conditional[] sites - each gated on its LaunchOptions toggle.
    const auto& opts = game::GetLaunchOptions();

    if (opts.multiInstance) {
        InstallSite(siteBypassMultiInstance, BYPASS_MULTI_INSTANCE_RVA,
                    reinterpret_cast<void (*)()>(&BypassMultiInstanceCheck), 6, /*isJmp=*/false);
        InstallSite(siteCreateWindowTitled, CREATE_WINDOW_TITLED_RVA,
                    reinterpret_cast<void (*)()>(&CreateGameWindowWithTitle), 6, /*isJmp=*/false);
    }
    if (opts.randomizeBnetCache) {
        InstallSite(siteBnetCache1, BNET_CACHE_1_RVA, reinterpret_cast<void (*)()>(&OpenPerInstanceBnetCache), 6,
                    /*isJmp=*/false);
        InstallSite(siteBnetCache2, BNET_CACHE_2_RVA, reinterpret_cast<void (*)()>(&OpenPerInstanceBnetCache), 6,
                    /*isJmp=*/false);
    }
    if (opts.reduceFailToJoin) {
        InstallSite(siteFailToJoinBackoff, FAIL_TO_JOIN_BACKOFF_RVA, &BypassFailToJoinBackoffThunk, 6,
                    /*isJmp=*/false);
    }
    if (classicCdKey != nullptr) {
        InstallSite(siteClassicCdKey, CLASSIC_CDKEY_RVA, &InjectClassicCdKeyThunk, 5, /*isJmp=*/true);
    }
    if (lodCdKey != nullptr) {
        InstallSite(siteLodCdKey, LOD_CDKEY_RVA, &InjectLodCdKeyThunk, 5, /*isJmp=*/true);
    }
}

namespace detail {

ClickActionScope::ClickActionScope(D2UnitStrc* unit) {
    clickActionUnit.store(unit, std::memory_order_release);
    clickActionActive.store(true, std::memory_order_release);
}

ClickActionScope::~ClickActionScope() {
    clickActionActive.store(false, std::memory_order_release);
    clickActionUnit.store(nullptr, std::memory_order_release);
}

}  // namespace detail

void RemoveAll() {
    if (moduleBase == 0) {
        return;
    }

    // Reverse install order - Conditional[] first (they were installed last).
    RestoreSite(siteLodCdKey, LOD_CDKEY_RVA, 5);
    RestoreSite(siteClassicCdKey, CLASSIC_CDKEY_RVA, 5);
    RestoreSite(siteFailToJoinBackoff, FAIL_TO_JOIN_BACKOFF_RVA, 6);
    RestoreSite(siteBnetCache2, BNET_CACHE_2_RVA, 6);
    RestoreSite(siteBnetCache1, BNET_CACHE_1_RVA, 6);
    RestoreSite(siteCreateWindowTitled, CREATE_WINDOW_TITLED_RVA, 6);
    RestoreSite(siteBypassMultiInstance, BYPASS_MULTI_INSTANCE_RVA, 6);

    RestoreSite(siteP22, P22_RVA, 6);
    RestoreSite(siteP21, P21_RVA, 6);
    // P20 not installed - no restore needed.
    RestoreSite(siteP19, P19_RVA, 6);
    RestoreSite(siteP18, P18_RVA, 10);
    RestoreSite(siteP17, P17_RVA, 5);
    RestoreSite(siteP16, P16_RVA, 5);
    RestoreSite(siteP15, P15_RVA, 7);
    RestoreSite(siteP14, P14_RVA, 7);
    RestoreSite(siteP9, P9_RVA, 5);

    if (siteP8.installed) {
        RestoreByte(moduleBase + P8_RVA, siteP8.original[0]);
        siteP8.installed = false;
    }

    RestoreSite(siteP5, P5_RVA, 5);
    RestoreSite(siteP4, P4_RVA, 5);
    RestoreSite(siteP3, P3_RVA, 5);
    RestoreSite(siteP2, P2_RVA, 5);
    RestoreSite(siteP1, P1_RVA, 5);
}

}  // namespace d2bs::hooks::intercepts
