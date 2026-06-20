#include "hooks/HookManager.h"

#include <Windows.h>
#include <detours/detours.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>

#include "components/config/AppConfig.h"
#include "components/speedhack/Speedhack.h"
#include "console/Console.h"
#include "game/GameThread.h"
#include "hooks/InlinePatch.h"
#include "hooks/Intercepts.h"
#include "hooks/Socks5Proxy.h"
#include "imports/D2Gfx.h"
#include "utils/threadutils.h"

namespace d2bs::hooks {

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - hooks own module-level state by definition

const game::GameCallbacks* activeCallbacks = nullptr;

std::atomic<DWORD> gameThreadId{0};
std::atomic<bool> isInstalled{false};

// Detours-real-fn pointers (Detours rewrites these to point at the trampoline
// that calls the original implementation).
using SleepFn = VOID(WINAPI*)(DWORD);
using CursorLockFn = BOOL(__fastcall*)(int, int);
SleepFn realSleep = ::Sleep;
CursorLockFn realCursorLock = nullptr;

// Win32 hook handle
HHOOK getMsgHook = nullptr;

// Private markers tagging input this DLL injected (SendClick / SendKey /
// control clicks) so GetMsgProc lets it through while still blocking the
// human's hardware input. Chosen in bits real hardware leaves clear, and
// stripped before the game sees the message:
//   mouse: HIWORD(wParam) is unused for the button messages we post
//   key:   lParam bit 25 is a reserved keystroke-flag bit (zero on real input)
constexpr WPARAM INJECTED_MOUSE_TAG = 0x00010000;
constexpr LPARAM INJECTED_KEY_TAG = 0x02000000;

// WndProc subclass state
WNDPROC originalWndProc = nullptr;
HWND subclassedHwnd = nullptr;

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
    // Foreign threads (e.g. a staged loader DLL's workers) may lack this
    // module's TLS; the thread_locals below (inSleepCallback, and the
    // speedhack's waitChainDepth / threadOptIn via NestedWaitGuard /
    // ScaleTimeout) would access-violate. Pass straight through to the real
    // Sleep - no scaling, no onSleep drive.
    if (!thread_utils::HasThreadLocalStorage()) {
        realSleep(ms);
        return;
    }
    speedhack::NestedWaitGuard guard;
    if (inSleepCallback) {
        realSleep(ms);
        return;
    }

    const DWORD currentTid = GetCurrentThreadId();
    const DWORD captured = gameThreadId.load(std::memory_order_relaxed);
    if (captured == 0 || currentTid != captured) {
        // Either we don't yet know the game thread, or this Sleep is on a
        // script / worker thread. Scale and pass through.
        realSleep(speedhack::ScaleTimeout(ms));
        return;
    }
    inSleepCallback = true;
    if (activeCallbacks != nullptr && activeCallbacks->onSleep != nullptr) {
        activeCallbacks->onSleep(std::chrono::milliseconds{ms});
    } else {
        realSleep(speedhack::ScaleTimeout(ms));
    }
    inSleepCallback = false;
}

// ---------------------------------------------------------------------------
// Win32 input hook (WH_GETMESSAGE)
// ---------------------------------------------------------------------------
//
// A single WH_GETMESSAGE hook owns all keyboard/mouse handling. It runs inside
// the game's GetMessage/PeekMessage as a message is about to be returned and,
// unlike WH_MOUSE/WH_KEYBOARD, can rewrite the MSG. We never discard input:
// discarding makes D2's PeekMessage(PM_NOREMOVE)+blocking-GetMessage pump block
// on the swallowed message and freeze the frame loop while the cursor hovers.
// To block an input we rewrite it to WM_NULL - GetMessage still returns, the
// pump keeps turning, and the game ignores the no-op.

void Neutralize(MSG* msg) {
    msg->message = WM_NULL;
    msg->wParam = 0;
    msg->lParam = 0;
}

// Decide whether a human mouse message should be blocked, dispatching script
// mouse events as a side effect when it is not. Matching the reference,
// blockMouse suppresses script events too (the early return).
bool HandleMouseMessage(const MSG* msg) {
    if (config::GetAppConfig().blockMouse.load()) {
        return true;
    }
    if (activeCallbacks == nullptr) {
        return false;
    }
    // Client coords are packed (signed) into lParam for the client-area mouse
    // messages we dispatch. A negative coord means the window captured the mouse
    // and it was dragged outside the client area - leave those alone.
    const int32_t x = static_cast<int16_t>(msg->lParam & 0xFFFF);
    const int32_t y = static_cast<int16_t>((msg->lParam >> 16) & 0xFFFF);
    if (x < 0 || y < 0) {
        return false;
    }
    const auto pos = game::Position{
        .x = static_cast<uint32_t>(x),
        .y = static_cast<uint32_t>(y),
    };
    switch (msg->message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            // Middle-button intentionally omitted - `game::ClickButton` has no
            // Middle value. WM_MBUTTON* falls through to the outer `default`.
            if (activeCallbacks->onMouseClick == nullptr) {
                break;
            }
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool isLeft = (msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP);
            const bool isUp = (msg->message == WM_LBUTTONUP || msg->message == WM_RBUTTONUP);
            const game::ClickButton button = [&] {
                if (isLeft) {
                    return shift ? game::ClickButton::ShiftLeft : game::ClickButton::Left;
                }
                return shift ? game::ClickButton::ShiftRight : game::ClickButton::Right;
            }();
            const auto state = isUp ? game::KeyState::Up : game::KeyState::Down;
            return activeCallbacks->onMouseClick(button, pos, state);
        }
        case WM_MOUSEMOVE:
            if (activeCallbacks->onMouseMove != nullptr) {
                activeCallbacks->onMouseMove(pos);
            }
            break;
        default:
            break;
    }
    return false;
}

// Decide whether a human key transition should be blocked, dispatching
// onKeyEvent as a side effect when it is not. Character messages are handled by
// the caller and never reach here.
bool HandleKeyMessage(const MSG* msg) {
    if (config::GetAppConfig().blockKeys.load()) {
        return true;
    }
    if (activeCallbacks == nullptr || activeCallbacks->onKeyEvent == nullptr) {
        return false;
    }
    // lParam bit 31 = transition (0 down / 1 up), bit 30 = previous key state.
    const bool isRepeat = ((msg->lParam >> 31) & 1) == 0 && ((msg->lParam >> 30) & 1) != 0;
    if (isRepeat) {
        return false;
    }
    const bool isUp = (msg->lParam & (1U << 31)) != 0;
    const auto state = isUp ? game::KeyState::Up : game::KeyState::Down;
    return activeCallbacks->onKeyEvent(static_cast<uint32_t>(msg->wParam), state);
}

void ProcessMouse(MSG* msg) {
    // Only the button messages we inject can carry the tag: their HIWORD(wParam)
    // is unused, unlike WM_XBUTTON* / WM_MOUSEWHEEL where it holds data that
    // could alias the tag bit.
    const bool isButton = msg->message == WM_LBUTTONDOWN || msg->message == WM_LBUTTONUP ||
                          msg->message == WM_RBUTTONDOWN || msg->message == WM_RBUTTONUP;
    if (isButton && (msg->wParam & INJECTED_MOUSE_TAG) != 0) {
        msg->wParam &= ~INJECTED_MOUSE_TAG;  // our own click: strip tag, let it through
        return;
    }
    if (HandleMouseMessage(msg)) {
        Neutralize(msg);
    }
}

void ProcessKey(MSG* msg) {
    // Characters synthesized by TranslateMessage are always let through: when
    // blockKeys is on we neutralize untagged key-downs before TranslateMessage
    // runs, so any character still in the stream came from our own injected key.
    if (msg->message == WM_CHAR || msg->message == WM_SYSCHAR || msg->message == WM_DEADCHAR ||
        msg->message == WM_SYSDEADCHAR || msg->message == WM_UNICHAR) {
        return;
    }
    if ((msg->lParam & INJECTED_KEY_TAG) != 0) {
        msg->lParam &= ~INJECTED_KEY_TAG;  // our own key: strip tag, let it through
        return;
    }
    if (HandleKeyMessage(msg)) {
        Neutralize(msg);
    }
}

// WH_GETMESSAGE callback. wParam = PM_REMOVE / PM_NOREMOVE. Act only on
// PM_REMOVE: PeekMessage(PM_NOREMOVE) probes must still see the real message so
// D2's pump takes its drain-the-queue branch; we neutralize on removal.
LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && wParam == PM_REMOVE) {
        auto* msg = reinterpret_cast<MSG*>(lParam);
        if (msg != nullptr) {
            if (msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST) {
                ProcessMouse(msg);
            } else if (msg->message >= WM_KEYFIRST && msg->message <= WM_KEYLAST) {
                ProcessKey(msg);
            }
        }
    }
    return CallNextHookEx(getMsgHook, code, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WndProc subclass
// ---------------------------------------------------------------------------

LRESULT CALLBACK SubclassedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA && activeCallbacks != nullptr && activeCallbacks->onIPC != nullptr) {
        auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (cds != nullptr && cds->lpData != nullptr) {
            const auto mode = static_cast<game::IpcMode>(cds->dwData);
            const auto* bytes = static_cast<const char*>(cds->lpData);
            std::string payload(bytes, cds->cbData);
            // Senders typically include the trailing NUL in cbData (matches the
            // CF_TEXT / null-terminated C-string convention used by the
            // reference launcher). Strip it so JS string compares like
            // `msg === "Handle"` work without an embedded NUL throwing them off.
            if (!payload.empty() && payload.back() == '\0') {
                payload.pop_back();
            }
            activeCallbacks->onIPC(mode, payload);
        }
    }

    return CallWindowProcW(originalWndProc, hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Install / Remove helpers
// ---------------------------------------------------------------------------

void InstallDetoursHooks() {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    realCursorLock = reinterpret_cast<CursorLockFn>(base + CURSOR_LOCK_OFFSET);
    realSleep = ::Sleep;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&realCursorLock), reinterpret_cast<PVOID>(&NoOpCursorLock));
    DetourAttach(reinterpret_cast<PVOID*>(&realSleep), reinterpret_cast<PVOID>(&HookedSleep));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        // Failure leaves realSleep == ::Sleep (no trampoline): HookedSleep never
        // runs, onSleep never fires, GameLoop / chickening / drain all stall.
        // Bot is non-functional either way; log so the user can diagnose.
        spdlog::error("Detours install failed: {}", err);
    }

    // Separate transaction so a speedhack failure doesn't leave Sleep / cursor
    // hooks half-installed (and vice versa). Speedhack tolerates being un-
    // installed - bot still works, just no time scaling.
    speedhack::Install();

    // SOCKS5 proxy for the game's outbound Battle.net connections. Self-contained
    // (IAT patch on WS2_32 connect, not a Detours trampoline); no-op unless the
    // process was launched with -proxy. Independent of the transactions above.
    socks5::Install();
}

void RemoveDetoursHooks() {
    socks5::Remove();
    speedhack::Remove();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&realCursorLock), reinterpret_cast<PVOID>(&NoOpCursorLock));
    DetourDetach(reinterpret_cast<PVOID*>(&realSleep), reinterpret_cast<PVOID>(&HookedSleep));
    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        spdlog::error("Detours remove failed: {}", err);
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

    // SetWindowsHookEx ties the hook lifetime to the installing thread. Post to the game thread so the installer is
    // also the thread being hooked (it lives for the process lifetime).
    game::GameThread::Post([windowThread]() {
        getMsgHook = SetWindowsHookExW(WH_GETMESSAGE, &GetMsgProc, nullptr, windowThread);
        // Game thread is the canonical caller of the speedhack: scale its
        // time reads and waits so D2's frame timer reacts to the multiplier.
        speedhack::OptInCurrentThread();
    });

    subclassedHwnd = hwnd;
    originalWndProc = reinterpret_cast<WNDPROC>(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - Win32 SetWindowLongPtr returns LONG_PTR
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassedWndProc)));
}

void RemoveWin32Hooks() {
    if (getMsgHook != nullptr) {
        UnhookWindowsHookEx(getMsgHook);
        getMsgHook = nullptr;
    }
    if (subclassedHwnd != nullptr && originalWndProc != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - Win32 SetWindowLongPtr returns LONG_PTR
        SetWindowLongPtrW(subclassedHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));
        originalWndProc = nullptr;
        subclassedHwnd = nullptr;
    }
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

void PostInjectedInput(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
        wParam |= INJECTED_MOUSE_TAG;
    } else if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
        lParam |= INJECTED_KEY_TAG;
    }
    PostMessageW(hwnd, message, wParam, lParam);
}

}  // namespace d2bs::hooks
