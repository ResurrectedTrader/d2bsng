#include "console/Console.h"

#include <Windows.h>

#include <GL/gl.h>

#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_win32.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

#include "components/console/Console.h"
#include "imports/D2Gfx.h"
#include "utils/threadutils.h"

// imgui_impl_win32.h declares this for apps that own their own WndProc.
// Re-declared here so we don't depend on the header re-exporting it at
// file scope.
// NOLINTNEXTLINE(readability-identifier-naming) - matches upstream ImGui declaration
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace d2bs::console {

namespace {

constexpr int32_t INITIAL_WIDTH = 1000;
constexpr int32_t INITIAL_HEIGHT = 700;
constexpr int32_t TARGET_FRAME_MS = 16;
constexpr const wchar_t* WND_CLASS = L"d2bsng_console";
constexpr const wchar_t* WND_TITLE = L"d2bsng console";
constexpr const wchar_t* TITLE_SUFFIX = L" Console";

// Posted to the console hwnd whenever the game window's title changes.
// Source of truth is the game hwnd - the handler re-reads via GetWindowText.
constexpr UINT WM_GAME_TITLE_CHANGED = WM_APP + 1;

// Published once the render thread has a window. Show / Hide read this.
std::atomic<HWND> consoleHwnd{nullptr};

// Init idempotency guard + worker thread.
std::atomic<bool> running{false};
std::jthread renderThread;

// Game-window subclassing state. Once the game hwnd appears we replace its
// WndProc with TitleSubclassProc and stash the original so we can chain.
// All accessed from the render thread only (subclass install happens there;
// the subclass proc itself runs on the game thread but only reads these
// values, never writes).
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - module-level subclass state
HWND subclassedGameHwnd = nullptr;
WNDPROC originalGameWndProc = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Ctrl+Break console summon, fed by WM_INPUT (raw input). Raw input is a
// non-blocking notification, so it adds no system-wide input latency, and it is
// delivered per registering window, so multiple d2bs instances each receive their
// own copy. Each filters on the foreground game HWND, so only the focused instance
// reacts. It runs on the console thread, so it still fires while the game thread is
// hung. Being observe-only it cannot consume the key, so Ctrl+Break also reaches
// the focused game (D2 ignores VK_CANCEL).
void HandleRawKeyboard(LPARAM lp) {
    RAWINPUT ri{};
    UINT size = sizeof(ri);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) ==
        static_cast<UINT>(-1)) {
        return;
    }
    if (ri.header.dwType != RIM_TYPEKEYBOARD) {
        return;
    }
    // Key-down only; RI_KEY_BREAK marks the key-up transition.
    if ((ri.data.keyboard.Flags & RI_KEY_BREAK) != 0) {
        return;
    }
    // Ctrl+Break surfaces as VK_PAUSE or VK_CANCEL on the Pause/Break key.
    const USHORT vkey = ri.data.keyboard.VKey;
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && (vkey == VK_CANCEL || vkey == VK_PAUSE)) {
        HWND gameHwnd = d2bs::imports::d2gfx::WINDOW_GetWindow();
        if (gameHwnd != nullptr && gameHwnd == GetForegroundWindow()) {
            Show();
        }
    }
}

// Register or unregister this window for raw keyboard input: RIDEV_INPUTSINK with
// the target window to start, RIDEV_REMOVE with a null target to stop.
void SetRawKeyboardInput(HWND target, DWORD flags) {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;  // HID generic desktop page
    rid.usUsage = 0x06;      // keyboard
    rid.dwFlags = flags;
    rid.hwndTarget = target;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

struct GLState {
    HDC hdc = nullptr;
    HGLRC hrc = nullptr;
};

bool InitGL(HWND hwnd, GLState& gl) {
    gl.hdc = GetDC(hwnd);
    if (gl.hdc == nullptr) {
        return false;
    }
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int32_t pf = ChoosePixelFormat(gl.hdc, &pfd);
    if (pf == 0 || SetPixelFormat(gl.hdc, pf, &pfd) == FALSE) {
        ReleaseDC(hwnd, gl.hdc);
        gl.hdc = nullptr;
        return false;
    }

    gl.hrc = wglCreateContext(gl.hdc);
    if (gl.hrc == nullptr || wglMakeCurrent(gl.hdc, gl.hrc) == FALSE) {
        if (gl.hrc != nullptr) {
            wglDeleteContext(gl.hrc);
            gl.hrc = nullptr;
        }
        ReleaseDC(hwnd, gl.hdc);
        gl.hdc = nullptr;
        return false;
    }
    return true;
}

void DestroyGL(HWND hwnd, GLState& gl) {
    if (gl.hrc != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gl.hrc);
        gl.hrc = nullptr;
    }
    if (gl.hdc != nullptr) {
        ReleaseDC(hwnd, gl.hdc);
        gl.hdc = nullptr;
    }
}

// Reads the game window's current title, composes "<game> Console" into the
// console hwnd. No-op if the read fails or the composed title already matches.
// Runs on the console (render) thread.
void ApplyGameTitle(HWND consoleWnd) {
    HWND gameHwnd = d2bs::imports::d2gfx::WINDOW_GetWindow();
    if (gameHwnd == nullptr) {
        return;
    }
    std::array<wchar_t, 192> gameTitle{};
    const int32_t gameLen = GetWindowTextW(gameHwnd, gameTitle.data(), static_cast<int32_t>(gameTitle.size()));
    if (gameLen <= 0) {
        return;
    }

    std::array<wchar_t, 256> desired{};
    const size_t suffixLen = std::wcslen(TITLE_SUFFIX);
    if (static_cast<size_t>(gameLen) + suffixLen + 1 > desired.size()) {
        return;
    }
    std::memcpy(desired.data(), gameTitle.data(), static_cast<size_t>(gameLen) * sizeof(wchar_t));
    std::memcpy(desired.data() + gameLen, TITLE_SUFFIX, suffixLen * sizeof(wchar_t));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by guard above
    desired[static_cast<size_t>(gameLen) + suffixLen] = L'\0';

    std::array<wchar_t, 256> current{};
    const int32_t curLen = GetWindowTextW(consoleWnd, current.data(), static_cast<int32_t>(current.size()));
    if (curLen > 0 && std::wcscmp(current.data(), desired.data()) == 0) {
        return;
    }
    SetWindowTextW(consoleWnd, desired.data());
}

// Replacement WndProc installed on the game window. Runs on the game thread.
// On WM_SETTEXT (sent by SetWindowText / SetWindowTextW), pokes the console
// thread to re-read the new title. The original proc still does the actual
// title update; we just observe it.
LRESULT CALLBACK TitleSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WNDPROC orig = originalGameWndProc;
    const LRESULT r = (orig != nullptr) ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_SETTEXT) {
        if (HWND consoleWnd = consoleHwnd.load(std::memory_order_acquire); consoleWnd != nullptr) {
            // PostMessage is non-blocking; the console thread re-reads the
            // game title from the source of truth when it handles the message.
            PostMessageW(consoleWnd, WM_GAME_TITLE_CHANGED, 0, 0);
        }
    }
    return r;
}

// One-shot install of the title subclass on the game hwnd. Called every
// render-loop iteration; cheap fast-path when already installed. Replays
// the title once at install time so the initial state is mirrored without
// waiting for the first WM_SETTEXT.
void EnsureGameTitleSubclass(HWND consoleWnd) {
    HWND gameHwnd = d2bs::imports::d2gfx::WINDOW_GetWindow();
    if (gameHwnd == nullptr || gameHwnd == subclassedGameHwnd) {
        return;
    }
    auto prev = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(gameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&TitleSubclassProc)));
    if (prev == nullptr) {
        return;  // SetWindowLongPtr failed - leave state untouched, retry next frame.
    }
    originalGameWndProc = prev;
    subclassedGameHwnd = gameHwnd;
    ApplyGameTitle(consoleWnd);
}

LRESULT CALLBACK ConsoleWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp) != 0) {
        return 1;
    }
    switch (msg) {
        case WM_GAME_TITLE_CHANGED:
            ApplyGameTitle(hwnd);
            return 0;
        case WM_INPUT:
            // Raw-input keyboard event; check for the console hotkey. Fall through
            // to DefWindowProc so the raw-input buffer gets cleaned up.
            HandleRawKeyboard(lp);
            break;
        case WM_SYSCOMMAND:
            // Swallow Alt-as-menu-trigger - it would eat keystrokes the REPL wants.
            if ((wp & 0xFFF0U) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            // Home hides when no ImGui widget owns the keyboard - when a text
            // input is focused, the keystroke is left for ImGui to interpret
            // as cursor-to-start-of-line.
            if (wp == VK_HOME && !ImGui::GetIO().WantCaptureKeyboard) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RenderLoop(const std::stop_token& stop) {
    d2bs::thread_utils::SetThreadDescription("d2bs console render");

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    // CS_OWNDC keeps a persistent device context per window - required so
    // the GL context we make current in InitGL stays valid for the window's life.
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &ConsoleWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = WND_CLASS;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (RegisterClassExW(&wc) == 0) {
        return;
    }

    HWND hwnd = CreateWindowExW(0, WND_CLASS, WND_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                INITIAL_WIDTH, INITIAL_HEIGHT, nullptr, nullptr, hInst, nullptr);
    if (hwnd == nullptr) {
        UnregisterClassW(WND_CLASS, hInst);
        return;
    }

    GLState gl;
    if (!InitGL(hwnd, gl)) {
        DestroyWindow(hwnd);
        UnregisterClassW(WND_CLASS, hInst);
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplOpenGL2_Init();

    // Window is created hidden - first show is driven by Home (framework
    // hotkey) or by an explicit JS showConsole() call. Keeps the console
    // out of the user's face during D2 startup.
    consoleHwnd.store(hwnd, std::memory_order_release);

    // Receive keyboard input even when this (hidden, background) window isn't
    // focused, so Ctrl+Break can summon the console while the game thread is hung.
    SetRawKeyboardInput(hwnd, RIDEV_INPUTSINK);

    using std::chrono::milliseconds;
    using std::chrono::steady_clock;
    auto nextFrame = steady_clock::now();

    while (!stop.stop_requested()) {
        MSG msg;
        bool quitting = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                quitting = true;
            }
        }
        if (quitting) {
            break;
        }

        // Subclass the game hwnd as soon as it appears; the subclass forwards
        // subsequent WM_SETTEXT events to us via WM_GAME_TITLE_CHANGED. The
        // install also performs the initial mirror so we don't have to wait
        // for the first game-title write.
        EnsureGameTitleSubclass(hwnd);

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        d2bs::framework::console::DrawFrame();

        ImGui::Render();

        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);
        glViewport(0, 0, static_cast<GLsizei>(clientRect.right - clientRect.left),
                   static_cast<GLsizei>(clientRect.bottom - clientRect.top));
        glClearColor(0.10F, 0.10F, 0.12F, 1.00F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(gl.hdc);

        nextFrame += milliseconds(TARGET_FRAME_MS);
        std::this_thread::sleep_until(nextFrame);
    }

    // Unregister raw input (RIDEV_REMOVE requires a null hwndTarget).
    SetRawKeyboardInput(nullptr, RIDEV_REMOVE);

    // Detach the subclass before tearing down. If the game hwnd is still
    // ours, restore the original WndProc; otherwise the game thread may keep
    // dispatching messages into freed code after this DLL unloads.
    if (subclassedGameHwnd != nullptr && originalGameWndProc != nullptr) {
        SetWindowLongPtrW(subclassedGameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalGameWndProc));
        subclassedGameHwnd = nullptr;
        originalGameWndProc = nullptr;
    }

    consoleHwnd.store(nullptr, std::memory_order_release);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyGL(hwnd, gl);
    DestroyWindow(hwnd);
    UnregisterClassW(WND_CLASS, hInst);
}

}  // namespace

void Init() {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        return;
    }
    renderThread = std::jthread(&RenderLoop);
}

void Show() {
    if (HWND h = consoleHwnd.load(std::memory_order_acquire); h != nullptr) {
        // SW_RESTORE covers both hidden and minimized states.
        ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
    }
}

void Hide() {
    if (HWND h = consoleHwnd.load(std::memory_order_acquire); h != nullptr) {
        ShowWindow(h, SW_HIDE);
    }
}

void Toggle() {
    HWND h = consoleHwnd.load(std::memory_order_acquire);
    if (h == nullptr) {
        return;
    }
    if (IsWindowVisible(h) != FALSE) {
        Hide();
    } else {
        Show();
    }
}

}  // namespace d2bs::console
