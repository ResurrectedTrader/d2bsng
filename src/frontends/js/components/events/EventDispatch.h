#pragma once

#include <v8.h>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "game/Types.h"

namespace d2bs {

namespace events {

// Handlers registered for one event name, across all scripts. Dispatchers check this before
// building an event, which is the expensive half - a packet event copies the packet and every
// blockable event allocates dispatch bookkeeping. Counts handlers regardless of script state,
// so it can only over-report; FireIfRunning still filters per script.
class ListenerCount {
   public:
    static ListenerCount& For(std::string_view eventName);

    void Add(int32_t delta) { count_.fetch_add(delta, std::memory_order_release); }
    bool Any() const { return count_.load(std::memory_order_acquire) > 0; }

   private:
    std::atomic<int32_t> count_{0};
};

}  // namespace events

// Non-blockable event dispatchers
void LifeEventDispatch(uint32_t life);
void ManaEventDispatch(uint32_t mana);
void PlayerAssignEventDispatch(uint32_t unitId);
void MouseClickEventDispatch(game::ClickButton button, game::Position pos, game::KeyState state);
void MouseMoveEventDispatch(game::Position pos);
void ItemActionEventDispatch(uint32_t unitId, uint32_t action, const std::string& code, bool isGlobal);
void GameActionEventDispatch(int32_t mode, uint32_t param1, uint32_t param2, const std::string& name1,
                             const std::string& name2);
void CopyDataEventDispatch(game::IpcMode mode, const std::string& payload);
void ScriptBroadcastEventDispatch(const v8::FunctionCallbackInfo<v8::Value>& args);

// Blockable event dispatchers (return true if event was blocked)
bool KeyDownUpEventDispatch(uint32_t key, game::KeyState state);
bool ChatEventDispatch(const std::string& sender, const std::string& message);
bool ChatInputEventDispatch(const std::string& message);
bool WhisperEventDispatch(const std::string& sender, const std::string& message);
bool GamePacketEventDispatch(std::span<const uint8_t> packet);
bool GamePacketSentEventDispatch(std::span<const uint8_t> packet);
bool RealmPacketEventDispatch(std::span<const uint8_t> packet);

}  // namespace d2bs
