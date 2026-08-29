#include "EventDispatch.h"

#include <Windows.h>

#include <chrono>
#include <map>
#include <mutex>
#include <utility>

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

namespace events {

ListenerCount& ListenerCount::For(std::string_view eventName) {
    // Slots are never erased, so a returned reference stays valid - and a name always resolves to
    // the same slot, so register/unregister can never land on different counters. addEventListener
    // passes whatever string a script gives it, hence both bounds; anything past them shares one
    // slot, which over-reports and so only costs a build that FireIfRunning then filters.
    constexpr size_t MAX_SLOTS = 256;
    constexpr size_t MAX_NAME = 64;
    // Never destroyed: dispatchers cache a reference to a slot for the life of the process,
    // and D2's hooks can still fire after static destructors have run at DLL detach.
    static auto& mutex = *new std::mutex;
    static auto& slots = *new std::map<std::string, ListenerCount, std::less<>>;
    static auto& overflow = *new ListenerCount;

    std::scoped_lock lock(mutex);
    if (auto it = slots.find(eventName); it != slots.end()) {
        return it->second;
    }
    if (slots.size() >= MAX_SLOTS || eventName.size() > MAX_NAME) {
        return overflow;
    }
    return slots.try_emplace(std::string(eventName)).first->second;
}

}  // namespace events

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

template <typename EventT, typename... Args>
static std::shared_ptr<EventT> FireIfListening(Args&&... args) {
    // Resolved once per event type rather than per dispatch: this runs on D2's packet and input
    // paths, and For() takes a process-wide mutex that script threads also hold while allocating.
    static events::ListenerCount& listeners = events::ListenerCount::For(EventT::EVENT_NAME);
    if (!listeners.Any()) {
        return nullptr;
    }
    auto evt = std::make_shared<EventT>(std::forward<Args>(args)...);
    FireIfRunning(evt);
    return evt;
}

void LifeEventDispatch(uint32_t life) {
    FireIfListening<LifeEvent>(life);
}

void ManaEventDispatch(uint32_t mana) {
    FireIfListening<ManaEvent>(mana);
}

void PlayerAssignEventDispatch(uint32_t unitId) {
    FireIfListening<PlayerAssignEvent>(unitId);
}

void MouseClickEventDispatch(game::ClickButton button, game::Position pos, game::KeyState state) {
    FireIfListening<MouseClickEvent>(button, pos, state);
}

void MouseMoveEventDispatch(game::Position pos) {
    if (pos.x < 1 || pos.y < 1)
        return;
    FireIfListening<MouseMoveEvent>(pos);
}

void ItemActionEventDispatch(uint32_t unitId, uint32_t action, const std::string& code, bool isGlobal) {
    FireIfListening<ItemActionEvent>(unitId, action, code, isGlobal);
}

void GameActionEventDispatch(int32_t mode, uint32_t param1, uint32_t param2, const std::string& name1,
                             const std::string& name2) {
    FireIfListening<GameActionEvent>(mode, param1, param2, name1, name2);
}

void CopyDataEventDispatch(game::IpcMode mode, const std::string& payload) {
    FireIfListening<CopyDataEvent>(mode, payload);
}

void ScriptBroadcastEventDispatch(const v8::FunctionCallbackInfo<v8::Value>& args) {
    // Not probed: constructing this serialises the arguments, which can run script code and
    // throw out of scriptBroadcast(), so skipping it when nobody listens would be observable.
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
        FireIfListening<KeyUpEvent>(key);
        auto blocker = FireIfListening<KeyUpBlockerEvent>(key);
        return blocker && blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
    }
    FireIfListening<KeyDownEvent>(key);
    auto blocker = FireIfListening<KeyDownBlockerEvent>(key);
    return blocker && blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool ChatEventDispatch(const std::string& sender, const std::string& message) {
    FireIfListening<ChatEvent>(sender, message);
    auto blocker = FireIfListening<ChatBlockerEvent>(sender, message);
    return blocker && blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
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

    FireIfListening<ChatInputEvent>(message);
    auto blocker = FireIfListening<ChatInputBlockerEvent>(message);
    return blocker && blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool WhisperEventDispatch(const std::string& sender, const std::string& message) {
    FireIfListening<WhisperEvent>(sender, message);
    auto blocker = FireIfListening<WhisperBlockerEvent>(sender, message);
    return blocker && blocker->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool GamePacketEventDispatch(std::span<const uint8_t> packet) {
    auto evt = FireIfListening<GamePacketEvent>(packet);
    return evt && evt->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool GamePacketSentEventDispatch(std::span<const uint8_t> packet) {
    auto evt = FireIfListening<GamePacketSentEvent>(packet);
    return evt && evt->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

bool RealmPacketEventDispatch(std::span<const uint8_t> packet) {
    auto evt = FireIfListening<RealmPacketEvent>(packet);
    return evt && evt->IsBlocked(BLOCKABLE_WAIT).value_or(false);
}

}  // namespace d2bs
