#include "input/InputHook.h"

#include <Windows.h>

#include <cstdint>
#include <string>

#include "config/AppConfig.h"
#include "game/GameThread.h"

namespace d2bs::input {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - hooks own module-level state by definition

Hooks activeHooks;

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

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Sent to the subclassed window by Remove() so the unhook runs on the window's
// thread, and how long Remove() waits for that thread to answer.
constexpr UINT WM_REMOVE_INPUT_HOOKS = WM_APP + 0x2B5;
constexpr UINT REMOVE_TIMEOUT_MS = 2000;

// Retires the hook and the subclass. The window's thread is the only one that
// runs GetMsgProc (a thread-specific hook) and the subclassed WndProc, so on
// that thread neither can be mid-call while this runs.
void RemoveOnWindowThread() {
    if (getMsgHook != nullptr) {
        UnhookWindowsHookEx(getMsgHook);
        getMsgHook = nullptr;
    }
    // Only the restore is conditional. Clearing the state is not: if the
    // subclass never took, originalWndProc is null while subclassedHwnd is set,
    // and leaving it set would make Install() believe it is still hooked - the
    // window could then never be hooked again, and every Remove() would pay the
    // full send timeout waiting for a WndProc that never saw our message.
    if (subclassedHwnd != nullptr && originalWndProc != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - Win32 SetWindowLongPtr returns LONG_PTR
        SetWindowLongPtrW(subclassedHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));
    }
    originalWndProc = nullptr;
    subclassedHwnd = nullptr;
    activeHooks = {};
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
            // Loaded once - Remove() can clear the table between the guard
            // and the call.
            auto* const onMouseClick = activeHooks.onMouseClick;
            if (onMouseClick == nullptr) {
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
            return onMouseClick(button, pos, state);
        }
        case WM_MOUSEMOVE:
            if (auto* const onMouseMove = activeHooks.onMouseMove; onMouseMove != nullptr) {
                onMouseMove(pos);
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
    auto* const onKeyEvent = activeHooks.onKeyEvent;
    if (onKeyEvent == nullptr) {
        return false;
    }
    // lParam bit 31 = transition (0 down / 1 up), bit 30 = previous key state.
    const bool isRepeat = ((msg->lParam >> 31) & 1) == 0 && ((msg->lParam >> 30) & 1) != 0;
    if (isRepeat) {
        return false;
    }
    const bool isUp = (msg->lParam & (1U << 31)) != 0;
    const auto state = isUp ? game::KeyState::Up : game::KeyState::Down;
    return onKeyEvent(static_cast<uint32_t>(msg->wParam), state);
}

// The backend's first look at a human message. True when it consumed the
// message, which is then neutralised for the game.
bool Intercept(MSG* msg) {
    auto* const intercept = activeHooks.intercept;
    if (intercept == nullptr || !intercept(*msg)) {
        return false;
    }
    Neutralize(msg);
    return true;
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
    if (Intercept(msg)) {
        return;
    }
    if (HandleMouseMessage(msg)) {
        Neutralize(msg);
    }
}

void ProcessKey(MSG* msg) {
    if ((msg->lParam & INJECTED_KEY_TAG) != 0) {
        msg->lParam &= ~INJECTED_KEY_TAG;  // our own key: strip tag, let it through
        return;
    }
    if (Intercept(msg)) {
        return;
    }
    // Characters synthesized by TranslateMessage are always let through: when
    // blockKeys is on we neutralize untagged key-downs before TranslateMessage
    // runs, so any character still in the stream came from our own injected key.
    if (msg->message == WM_CHAR || msg->message == WM_SYSCHAR || msg->message == WM_DEADCHAR ||
        msg->message == WM_SYSDEADCHAR || msg->message == WM_UNICHAR) {
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
    if (msg == WM_REMOVE_INPUT_HOOKS) {
        RemoveOnWindowThread();
        return 0;
    }
    if (auto* const onIPC = activeHooks.onIPC; msg == WM_COPYDATA && onIPC != nullptr) {
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
            onIPC(mode, payload);
        }
    }

    return CallWindowProcW(originalWndProc, hwnd, msg, wParam, lParam);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Install(HWND window, Hooks hooks) {
    if (subclassedHwnd != nullptr) {
        return;
    }
    activeHooks = hooks;

    // SetWindowsHookEx ties the hook lifetime to the installing thread. Post to
    // the game thread so the installer is also the thread being hooked (it
    // lives for the process lifetime).
    const DWORD windowThread = GetWindowThreadProcessId(window, nullptr);
    game::GameThread::Post(
        [windowThread]() { getMsgHook = SetWindowsHookExW(WH_GETMESSAGE, &GetMsgProc, nullptr, windowThread); });

    subclassedHwnd = window;
    originalWndProc = reinterpret_cast<WNDPROC>(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - Win32 SetWindowLongPtr returns LONG_PTR
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassedWndProc)));
}

// GameThread::Execute cannot carry the unhook to the window's thread: by the
// time the backend removes its hooks the frontend has shut its engine down and
// the per-frame drain no longer runs, so the post would never land. A sent
// message is processed by the window's thread as soon as it pumps, which the
// game does every frame.
void Remove() {
    if (subclassedHwnd == nullptr) {
        return;
    }
    if (GetWindowThreadProcessId(subclassedHwnd, nullptr) == GetCurrentThreadId()) {
        RemoveOnWindowThread();
        return;
    }
    DWORD_PTR result = 0;
    SendMessageTimeoutW(subclassedHwnd, WM_REMOVE_INPUT_HOOKS, 0, 0, SMTO_NORMAL | SMTO_ABORTIFHUNG, REMOVE_TIMEOUT_MS,
                        &result);
    if (subclassedHwnd == nullptr) {
        return;  // the window's thread handled it
    }
    // The window's thread is not pumping, the window is gone, or something
    // re-subclassed over us. Retire from here; a call already inside the hook
    // then finishes against cleared state.
    RemoveOnWindowThread();
}

void PostInjectedInput(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
        wParam |= INJECTED_MOUSE_TAG;
    } else if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
        lParam |= INJECTED_KEY_TAG;
    }
    PostMessageW(hwnd, message, wParam, lParam);
}

}  // namespace d2bs::input
