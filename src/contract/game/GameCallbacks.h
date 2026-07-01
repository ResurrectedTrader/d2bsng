#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string>

#include "game/Types.h"

namespace d2bs::game {

// Forward declaration; full definition in game/Console.h. Used only by const-ref
// in the onConsoleMessage signature below, so the incomplete type suffices here.
namespace console {
struct Message;
}  // namespace console

// Callback table the frontend populates; the game layer invokes these from hooks, patches, and WndProc.
struct GameCallbacks {
    // --- Input (blockable) ---
    // Called from the keyboard hook / WndProc for key down/up events.
    // Return true to block the key from reaching the game.
    bool (*onKeyEvent)(uint32_t key, KeyState state) = nullptr;

    // Called from the mouse hook / WndProc for mouse click events.
    // Return true to block the click from reaching the game.
    bool (*onMouseClick)(ClickButton button, Position pos, KeyState state) = nullptr;

    // Called from the mouse hook / WndProc for mouse movement.
    void (*onMouseMove)(Position pos) = nullptr;

    // --- Chat (blockable) ---
    // Called when a chat message is received. Return true to block.
    // For 1.14d this is parsed from packet 0x26, but other game versions
    // may deliver chat via different mechanisms.
    bool (*onChatMessage)(const std::string& sender, const std::string& msg) = nullptr;

    // Called when the player submits chat input. Return true to block.
    bool (*onChatInput)(const std::string& input) = nullptr;

    // Called when a whisper is received. Return true to block.
    bool (*onWhisper)(const std::string& sender, const std::string& msg) = nullptr;

    // Called when the user submits a line to a port-provided console UI
    // (overlay Enter, ImGui InputText enter, terminal stdin line, etc.).
    // Frontend dispatches built-ins + JS-eval fallback via
    // js::console::OnCommand. Fire-and-forget.
    void (*onConsoleInput)(const std::string& line) = nullptr;

    // --- Console ---
    // Port-side console sink: the backend's game::console::OnMessage forwards
    // here so the frontend console receives the entry, without the backend
    // depending on the frontend directly.
    void (*onConsoleMessage)(const console::Message& msg) = nullptr;

    // Render the frontend console UI. The port's console host invokes this once
    // per frame, between ImGui NewFrame and Render, with an ImGui context active.
    void (*onConsoleDrawFrame)() = nullptr;

    // --- Packets (blockable) ---
    // Called when a game packet is received from the server. Return true to block.
    bool (*onGamePacketReceived)(std::span<const uint8_t> data) = nullptr;

    // Called when a game packet is about to be sent. Return true to block.
    bool (*onGamePacketSent)(std::span<const uint8_t> data) = nullptr;

    // Called when a realm packet is received. Return true to block.
    bool (*onRealmPacket)(std::span<const uint8_t> data) = nullptr;

    // --- Game lifecycle ---
    // These are derived from game packets in 1.14d, but packet formats differ
    // between game versions - the game layer handles version-specific parsing.
    void (*onGameEvent)(int32_t mode, uint32_t param1, uint32_t param2, const std::string& name1,
                        const std::string& name2) = nullptr;
    void (*onItemAction)(uint32_t unitId, uint32_t action, const std::string& code, bool isGlobal) = nullptr;

    // Called when the client observes a monster's death. `unitId` is the dying
    // monster's GUID, still resolvable to a unit so the framework can read its
    // class / super-unique identity. 1.14d derives this from the monster
    // mode-update packet (0x69) death action; other versions decode it their own
    // way. Fire-and-forget.
    void (*onMonsterDeath)(uint32_t unitId) = nullptr;

    // Called when an IPC message is received from another instance.
    // Reference/d2bs/D2Handlers.cpp:179-200 extracts WM_COPYDATA's dwData as the
    // mode ID. The framework handler switches on IpcMode for the two reserved
    // framework-level messages and falls through to the script-facing
    // CopyDataEvent for anything else; custom script modes flow as
    // static_cast<IpcMode>(raw) and fall into the default branch.
    void (*onIPC)(IpcMode mode, const std::string& payload) = nullptr;

    // --- Rendering (called from the game's render thread) ---
    // Called from the game thread's Sleep hook with the requested sleep duration.
    // The framework (GameLoop::OnSleep) drives per-frame work and drains the
    // game-thread queue in 1ms slices for the full duration, releasing the write
    // lock between slices so script readers can make progress.
    //
    // Reentrancy contract for the per-version Sleep hook:
    //   - The hook MUST use a thread_local bool reentrancy guard. On the outer
    //     entry (called by the game), it invokes this callback. When the callback
    //     itself calls ::Sleep, the hook fires again on the same thread; the
    //     guard must detect the re-entry and pass straight through to the real
    //     Sleep without re-invoking this callback.
    //   - The hook MUST also skip this callback when GetCurrentThreadId() is not
    //     the game thread (pass through to real Sleep).
    void (*onSleep)(std::chrono::milliseconds duration) = nullptr;

    // Called from the game's render function.
    // Framework flushes drawables so overlays composite with the current frame.
    void (*onDraw)() = nullptr;
};

// Install all game hooks and bind the callback table.
void InstallHooks(const GameCallbacks& callbacks);

// Remove all game hooks. Call during shutdown.
void RemoveHooks();

}  // namespace d2bs::game
