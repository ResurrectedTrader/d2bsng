#include "EventDispatch.h"

#include <Windows.h>

#include <chrono>

#include "Events.h"
#include "components/script/Commands.h"
#include "components/script/ScriptEngine.h"
#include "game/Console.h"

namespace d2bs {

using namespace std::chrono_literals;

// Per-dispatch wait budget on the network / game thread. Kept short so a
// slow script handler can't stall D2's network or input pipeline. If a
// script doesn't respond within BLOCKABLE_WAIT, the event is treated as
// "not blocked" and the game gets the packet / key / chat as normal.
// Toggle D2BSNG_BLOCKABLE_NO_WAIT in BlockableEvent.h to disable waiting
// entirely.
constexpr auto BLOCKABLE_WAIT = 50ms;

static void FireIfRunning(const std::shared_ptr<BaseEvent>& evt) {
    auto blocker = std::dynamic_pointer_cast<BlockableEvent>(evt);
    ScriptEngine::Instance().ForEachScript([&evt, &blocker](const std::shared_ptr<Script>& script) {
        if (script->GetState() == ScriptState::Running && script->IsEventRegistered(evt->Name())) {
            if (blocker) {
                // Increment before dispatch so Execute() can decrement.
                // If the dispatch is rejected (e.g. script transitioning to
                // Stopping), compensate immediately to avoid stalling the
                // game thread on IsBlocked().
                blocker->IncrementExpected();
                if (!script->ExecuteEvent(evt)) {
                    blocker->DecrementExpected();
                }
            } else {
                script->ExecuteEvent(evt);
            }
        }
    });
    if (blocker) {
        blocker->ResolveIfNoneExpected();
    }
}

void LifeEventDispatch(uint32_t life) {
    FireIfRunning(std::make_shared<LifeEvent>(life));
}

void ManaEventDispatch(uint32_t mana) {
    FireIfRunning(std::make_shared<ManaEvent>(mana));
}

void PlayerAssignEventDispatch(uint32_t unitId) {
    FireIfRunning(std::make_shared<PlayerAssignEvent>(unitId));
}

void MouseClickEventDispatch(game::ClickButton button, game::Position pos, game::KeyState state) {
    FireIfRunning(std::make_shared<MouseClickEvent>(button, pos, state));
}

void MouseMoveEventDispatch(game::Position pos) {
    if (pos.x < 1 || pos.y < 1)
        return;
    FireIfRunning(std::make_shared<MouseMoveEvent>(pos));
}

void ItemActionEventDispatch(uint32_t unitId, uint32_t action, const std::string& code, bool isGlobal) {
    FireIfRunning(std::make_shared<ItemActionEvent>(unitId, action, code, isGlobal));
}

void GameActionEventDispatch(int32_t mode, uint32_t param1, uint32_t param2, const std::string& name1,
                             const std::string& name2) {
    FireIfRunning(std::make_shared<GameActionEvent>(mode, param1, param2, name1, name2));
}

void CopyDataEventDispatch(game::IpcMode mode, const std::string& payload) {
    FireIfRunning(std::make_shared<CopyDataEvent>(mode, payload));
}

void ScriptBroadcastEventDispatch(const v8::FunctionCallbackInfo<v8::Value>& args) {
    FireIfRunning(std::make_shared<BroadcastEvent>(args));
}

bool KeyDownUpEventDispatch(uint32_t key, game::KeyState state) {
    // Framework-owned hotkeys run before JS dispatch and swallow the key
    // so scripts and the game both see nothing. Today: Home -> console
    // toggle. Mirrors reference/d2bs/D2Handlers.cpp:226-234, except the
    // chat/esc-menu gate isn't applicable - our console is a separate
    // window, not an in-game overlay.
    if (state == game::KeyState::Down && key == VK_HOME) {
        game::console::Toggle();
        return /* blocked */ true;
    }

    if (state == game::KeyState::Up) {
        FireIfRunning(std::make_shared<KeyUpEvent>(key));
        auto blocker = std::make_shared<KeyUpBlockerEvent>(key);
        FireIfRunning(blocker);
        return blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
    }
    FireIfRunning(std::make_shared<KeyDownEvent>(key));
    auto blocker = std::make_shared<KeyDownBlockerEvent>(key);
    FireIfRunning(blocker);
    return blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool ChatEventDispatch(const std::string& sender, const std::string& message) {
    FireIfRunning(std::make_shared<ChatEvent>(sender, message));
    auto blocker = std::make_shared<ChatBlockerEvent>(sender, message);
    FireIfRunning(blocker);
    return blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool ChatInputEventDispatch(const std::string& message) {
    // Command shortcut: '.'-prefixed chat never reaches the game; the rest
    // of the line dispatches through the framework console. Matches
    // reference/d2bs/D2Handlers.cpp:127 behavior - unknown dot-commands also
    // stay consumed (they fall through to the JS-eval path inside OnCommand,
    // which logs any ReferenceError via the EvaluateEvent path).
    if (!message.empty() && message[0] == '.') {
        js::script::RunCommand(message.substr(1));
        return /* block packet */ true;
    }

    FireIfRunning(std::make_shared<ChatInputEvent>(message));
    auto blocker = std::make_shared<ChatInputBlockerEvent>(message);
    FireIfRunning(blocker);
    return blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool WhisperEventDispatch(const std::string& sender, const std::string& message) {
    FireIfRunning(std::make_shared<WhisperEvent>(sender, message));
    auto blocker = std::make_shared<WhisperBlockerEvent>(sender, message);
    FireIfRunning(blocker);
    return blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool GamePacketEventDispatch(std::span<const uint8_t> packet) {
    auto evt = std::make_shared<GamePacketEvent>(packet);
    FireIfRunning(evt);
    return evt->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool GamePacketSentEventDispatch(std::span<const uint8_t> packet) {
    auto evt = std::make_shared<GamePacketSentEvent>(packet);
    FireIfRunning(evt);
    return evt->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool RealmPacketEventDispatch(std::span<const uint8_t> packet) {
    auto evt = std::make_shared<RealmPacketEvent>(packet);
    FireIfRunning(evt);
    return evt->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

}  // namespace d2bs
