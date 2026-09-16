#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

#include "game/Types.h"

// Game-window input plumbing shared by every backend (see
// docs/window_message_handling.md):
//   * a WH_GETMESSAGE hook on the game thread that dispatches the human's mouse
//     and key messages to the callbacks below and honours the blockMouse /
//     blockKeys settings - a blocked message is rewritten to WM_NULL, never
//     discarded, so the game's message pump keeps turning
//   * tagging of injected input (PostInjectedInput) so the bot's own clicks and
//     keystrokes pass the hook while the human's are blocked
//   * a WndProc subclass on the game window that forwards WM_COPYDATA

namespace d2bs::input {

// Plain function pointers, not std::function: Remove() clears the table while
// the window's thread may be inside a dispatch, and tearing down a std::function
// under a caller destroys the callable it is executing.
struct Hooks {
    // A human mouse click; return true to block it from reaching the game. Left
    // and right buttons only, with the shift modifier folded into the button.
    bool (*onMouseClick)(game::ClickButton button, game::Position pos, game::KeyState state) = nullptr;

    // A human mouse move over the client area.
    void (*onMouseMove)(game::Position pos) = nullptr;

    // A human key transition (auto-repeats are skipped); return true to block
    // it from reaching the game.
    bool (*onKeyEvent)(uint32_t key, game::KeyState state) = nullptr;

    // First look at every human mouse / key message, ahead of the block /
    // dispatch decision. Return true when the message was consumed; it is then
    // neutralised for the game and dispatched to nothing else.
    bool (*intercept)(const MSG& msg) = nullptr;

    // A WM_COPYDATA message on the game window. The trailing NUL senders
    // include in cbData is stripped from the payload.
    void (*onIPC)(game::IpcMode mode, const std::string& payload) = nullptr;
};

// Install the WH_GETMESSAGE hook on the window's owning thread and subclass the
// window's WndProc. SetWindowsHookEx ties a hook's lifetime to the installing
// thread, so the registration is posted to the game thread through
// GameThread::Post and lands on its next drain. Idempotent until Remove().
void Install(HWND window, Hooks hooks);

// Unhook and restore the original WndProc. Idempotent. Runs on the window's
// thread (delivered as a sent message the subclass handles) so it cannot race
// a call already inside the hook procedure or the subclass; if that thread does
// not answer within a bounded wait, the caller's thread does it instead.
void Remove();

// Post synthetic input (clicks / keys) to the game window. Tagged so the hook
// lets it through even while blockKeys / blockMouse suppresses the human's
// hardware input.
void PostInjectedInput(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

}  // namespace d2bs::input
