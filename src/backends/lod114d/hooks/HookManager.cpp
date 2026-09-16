#include "hooks/HookManager.h"

#include <Windows.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>

#include "console/Console.h"
#include "detour/Hook.h"
#include "game/GameThread.h"
#include "game/LaunchOptions.h"
#include "hooks/Intercepts.h"
#include "hooks/Realms.h"
#include "imports/D2Gfx.h"
#include "input/InputHook.h"
#include "proxy/Socks5Proxy.h"
#include "speedhack/Speedhack.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::hooks {

namespace {

spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("hooks.manager");
    return *LOGGER;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - hooks own module-level state by definition

const game::GameCallbacks* activeCallbacks = nullptr;

std::atomic<DWORD> gameThreadId{0};
std::atomic isInstalled{false};

using SleepFn = VOID(WINAPI*)(DWORD);
using CursorLockFn = BOOL(__fastcall*)(int, int);

VOID WINAPI HookedSleep(DWORD ms);
BOOL __fastcall NoOpCursorLock(int /*X*/, int /*Y*/);

detour::Hook<SleepFn> sleepHook{Sleep, &HookedSleep};
// The cursor-lock site is an offset into the game module, so it is only known
// once the module base is read at install time.
detour::Hook<CursorLockFn> cursorLockHook{&NoOpCursorLock};

// Sleep-hook reentrancy guard (per-thread). When the framework's onSleep
// callback itself invokes ::Sleep (its drain loop sleeps in 1ms slices), the
// hook fires re-entrantly on the same thread; this flag short-circuits to
// the trampoline so we don't recurse into the framework.
thread_local bool inSleepCallback = false;

// cursor-lock site
constexpr uintptr_t CURSOR_LOCK_OFFSET = 0x68770;

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// ---------------------------------------------------------------------------
// Detours hooks
// ---------------------------------------------------------------------------

// Replace `sub_468770` (a SetCursorPos wrapper the game uses to lock the OS
// cursor to its desired position during certain UI events) with a no-op so
// script-driven cursor moves aren't fought back.
BOOL __fastcall NoOpCursorLock(int /*X*/, int /*Y*/) {
    return TRUE;
}

// kernel32!Sleep replacement - drives `onSleep` for chicken / GameLoop /
// GameThread::Drain. Re-entrancy contract per GameCallbacks.h:92-99:
//   - thread_local guard prevents recursion when onSleep itself sleeps
//   - off-game-thread calls pass straight through to the trampoline
//
// Speedhack interaction: NestedWaitGuard wraps the entire body so the chain
// into SleepEx (which is also hooked) doesn't re-scale a value we already
// scaled here. Game-thread main Sleep is handed to onSleep, whose drain uses
// steady_clock - already scaled via QPC. Reentrant Sleep relies on the inner
// loop wrapping its sleep_for in SpeedhackDisabledScope to get real-ms slicing.
VOID WINAPI HookedSleep(DWORD ms) {
    // Two fast bailouts straight to the real Sleep (no scaling, no onSleep drive):
    //   - ms == 0 is a bare thread yield, not a real sleep. Running it through
    //     onSleep's frame drain turns a cheap yield into real work, so a game
    //     that spams Sleep(0) burns a lot of CPU. Just yield.
    //   - Foreign threads (e.g. a staged loader DLL's workers) may lack this
    //     module's TLS; the thread_locals below (inSleepCallback, and the
    //     speedhack's waitChainDepth / threadOptIn via NestedWaitGuard /
    //     ScaleTimeout) would access-violate.
    if (ms < 1 || !thread_utils::HasThreadLocalStorage()) {
        sleepHook(ms);
        return;
    }
    speedhack::NestedWaitGuard guard;
    if (inSleepCallback) {
        sleepHook(ms);
        return;
    }

    const DWORD currentTid = GetCurrentThreadId();
    const DWORD captured = gameThreadId.load(std::memory_order_relaxed);
    if (captured == 0 || currentTid != captured) {
        // Either we don't yet know the game thread, or this Sleep is on a
        // script / worker thread. Scale and pass through.
        sleepHook(speedhack::ScaleTimeout(ms));
        return;
    }
    inSleepCallback = true;
    if (activeCallbacks != nullptr && activeCallbacks->onSleep != nullptr) {
        activeCallbacks->onSleep(std::chrono::milliseconds{ms});
    } else {
        sleepHook(speedhack::ScaleTimeout(ms));
    }
    inSleepCallback = false;
}

// ---------------------------------------------------------------------------
// Win32 input hook
// ---------------------------------------------------------------------------

// The human-input callbacks the shared game-window hook dispatches to. A null
// table entry leaves the matching hook empty.
input::Hooks BuildInputHooks(const game::GameCallbacks* callbacks) {
    input::Hooks hooks;
    if (callbacks != nullptr) {
        hooks.onMouseClick = callbacks->onMouseClick;
        hooks.onMouseMove = callbacks->onMouseMove;
        hooks.onKeyEvent = callbacks->onKeyEvent;
        hooks.onIPC = callbacks->onIPC;
    }
    return hooks;
}

// ---------------------------------------------------------------------------
// Install / Remove helpers
// ---------------------------------------------------------------------------

void InstallDetoursHooks() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    cursorLockHook.SetTarget(reinterpret_cast<CursorLockFn>(base + CURSOR_LOCK_OFFSET));

    if (const int32_t err = detour::AttachAll({&cursorLockHook, &sleepHook}); err != 0) {
        // Failure leaves sleepHook reaching ::Sleep directly: HookedSleep never
        // runs, onSleep never fires, GameLoop / chickening / drain all stall.
        // Bot is non-functional either way; log so the user can diagnose.
        Log().error("Detours install failed: {}", err);
    }

    // Separate transaction so a speedhack failure doesn't leave Sleep / cursor
    // hooks half-installed (and vice versa). Speedhack tolerates being un-
    // installed - bot still works, just no time scaling.
    speedhack::Install();

    // SOCKS5 proxy for the game's outbound Battle.net connections. Self-contained:
    // it Detours the WS2_32 connect (plus gethostbyname / getpeername / closesocket)
    // in its own transaction, independent of the ones above. No-op unless launched
    // with -proxy.
    proxy::socks5::Install(game::GetLaunchOptions().proxy.value_or(std::string{}));

    // Inject framework realms into D2's in-memory server list (detours the Storm
    // registry read/write helpers). Must precede the client's first list read.
    realms::Install();
}

void RemoveDetoursHooks() {
    realms::Remove();
    proxy::socks5::Remove();
    speedhack::Remove();

    if (const int32_t err = detour::DetachAll({&cursorLockHook, &sleepHook}); err != 0) {
        Log().error("Detours remove failed: {}", err);
    }
}

void InstallWin32Hooks() {
    // Poll for the game window; it may not exist yet if d2bs is injected during D2's startup.
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds{15};
    HWND hwnd = imports::d2gfx::WINDOW_GetWindow();
    while (hwnd == nullptr && Clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        hwnd = imports::d2gfx::WINDOW_GetWindow();
    }
    if (hwnd == nullptr) {
        // 15s elapsed without a window - the game probably isn't going to
        // come up. Skip the Win32 hooks; Sleep + cursor-lock are still in
        // place and the framework can degrade gracefully.
        return;
    }

    // Spawn the console render thread now that the game window exists.
    // Deferring until here keeps the console window from appearing before
    // the game does, which would otherwise steal focus during startup.
    console::Init();

    // The window's owner thread is the game thread. Capture it here so the Sleep hook gates immediately.
    const DWORD windowThread = GetWindowThreadProcessId(hwnd, nullptr);
    gameThreadId.store(windowThread, std::memory_order_relaxed);
    thread_utils::SetThreadDescription("d2 game thread", windowThread);

    input::Install(hwnd, BuildInputHooks(activeCallbacks));
    // Game thread is the canonical caller of the speedhack: scale its
    // time reads and waits so D2's frame timer reacts to the multiplier.
    game::GameThread::Post([]() { speedhack::OptInCurrentThread(); });
}

void RemoveWin32Hooks() {
    input::Remove();
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Install(const game::GameCallbacks* callbacks) {
    bool expected = false;
    if (!isInstalled.compare_exchange_strong(expected, true)) {
        return;
    }
    activeCallbacks = callbacks;
    InstallDetoursHooks();
    // Inline patches must land before the game's WinMain runs FindWindowA /
    // CreateWindowExA / etc. We may already have installed them eagerly from
    // DllMain (each site is idempotent on `installed`); re-running here is a
    // no-op in that case, and a safety net if the early-install path changes.
    intercepts::InstallAll();
    InstallWin32Hooks();
}

void Remove() {
    bool expected = true;
    if (!isInstalled.compare_exchange_strong(expected, false)) {
        return;
    }
    RemoveWin32Hooks();
    intercepts::RemoveAll();
    RemoveDetoursHooks();
    activeCallbacks = nullptr;
    gameThreadId.store(0, std::memory_order_relaxed);
}

std::optional<DWORD> GetGameThreadId() {
    const DWORD id = gameThreadId.load(std::memory_order_relaxed);
    if (id == 0) {
        return std::nullopt;
    }
    return id;
}

const game::GameCallbacks* GetActiveCallbacks() {
    return activeCallbacks;
}

}  // namespace d2bs::hooks
