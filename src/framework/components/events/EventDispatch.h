#pragma once

#include <v8.h>
#include <cstdint>
#include <span>
#include <string>

#include "game/Types.h"

namespace d2bs {

// Non-blockable event dispatchers
void LifeEventDispatch(uint32_t life);
void ManaEventDispatch(uint32_t mana);
void PlayerAssignEventDispatch(uint32_t unitId);
void MouseClickEventDispatch(d2bs::game::ClickButton button, d2bs::game::Position pos, d2bs::game::KeyState state);
void MouseMoveEventDispatch(d2bs::game::Position pos);
void ItemActionEventDispatch(uint32_t unitId, uint32_t action, const std::string& code, bool isGlobal);
void GameActionEventDispatch(int32_t mode, uint32_t param1, uint32_t param2, const std::string& name1,
                             const std::string& name2);
void CopyDataEventDispatch(d2bs::game::IpcMode mode, const std::string& payload);
void ScriptBroadcastEventDispatch(const v8::FunctionCallbackInfo<v8::Value>& args);

// Blockable event dispatchers (return true if event was blocked)
bool KeyDownUpEventDispatch(uint32_t key, d2bs::game::KeyState state);
bool ChatEventDispatch(const std::string& sender, const std::string& message);
bool ChatInputEventDispatch(const std::string& message);
bool WhisperEventDispatch(const std::string& sender, const std::string& message);
bool GamePacketEventDispatch(std::span<const uint8_t> packet);
bool GamePacketSentEventDispatch(std::span<const uint8_t> packet);
bool RealmPacketEventDispatch(std::span<const uint8_t> packet);

}  // namespace d2bs
