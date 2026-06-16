#pragma once

#include <v8.h>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "BaseEvent.h"
#include "BlockableEvent.h"
#include "api/core/V8Convert.h"
#include "components/script/Commands.h"
#include "game/Console.h"
#include "game/Types.h"

namespace d2bs {

// ============================================================================
// Game State Events
// ============================================================================

/// @event Player current life (HP) changed, or first observed.
/// @param life {number} - the player's current life (HP)
class LifeEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, life)};
    }

   public:
    explicit LifeEvent(uint32_t life) : life(life) {}
    const uint32_t life;
    [[nodiscard]] std::string_view Name() const override { return "melife"; }
};

/// @event Player mana changed or first observed.
/// @param mana {number} - the player's current mana
class ManaEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, mana)};
    }

   public:
    explicit ManaEvent(uint32_t mana) : mana(mana) {}
    const uint32_t mana;
    [[nodiscard]] std::string_view Name() const override { return "memana"; }
};

/// @event A player unit id became current or changed.
/// @param unitId {number} - the assigned player unit id
class PlayerAssignEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, unitId)};
    }

   public:
    explicit PlayerAssignEvent(uint32_t unitId) : unitId(unitId) {}
    const uint32_t unitId;
    [[nodiscard]] std::string_view Name() const override { return "playerassign"; }
};

// ============================================================================
// Input Events
// ============================================================================

/// @event A key was pressed (non-blocking notification).
/// @param key {number} - the virtual key code
class KeyDownEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, key)};
    }

   public:
    explicit KeyDownEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    [[nodiscard]] std::string_view Name() const override { return "keydown"; }
};

/// @event A key was released (non-blocking notification).
/// @param key {number} - the virtual key code
class KeyUpEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, key)};
    }

   public:
    explicit KeyUpEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    [[nodiscard]] std::string_view Name() const override { return "keyup"; }
};

/// @event A key was pressed.
/// @param key {number} - the virtual key code
/// @returns {boolean} - return true to block the key from the game
class KeyDownBlockerEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, key)};
    }

   public:
    explicit KeyDownBlockerEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    [[nodiscard]] std::string_view Name() const override { return "keydownblocker"; }
};

/// @event A key was released.
/// @param key {number} - the virtual key code
/// @returns {boolean} - return true to block the key from the game
class KeyUpBlockerEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, key)};
    }

   public:
    explicit KeyUpBlockerEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    [[nodiscard]] std::string_view Name() const override { return "keyupblocker"; }
};

/// @event Mouse button changed.
/// @param button {number} - the mouse button
/// @param x {number} - screen x
/// @param y {number} - screen y
/// @param up {number} - 1 on release, 0 on press
class MouseClickEvent : public BaseEvent {
   protected:
    // JS arg shape unchanged: (button:number, x:number, y:number, up:number 0/1).
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, static_cast<uint32_t>(button)),
            d2bs::api::v8_convert::ToV8(isolate, pos.x),
            d2bs::api::v8_convert::ToV8(isolate, pos.y),
            d2bs::api::v8_convert::ToV8(isolate, state == d2bs::game::KeyState::Up ? 1 : 0),
        };
    }

   public:
    MouseClickEvent(d2bs::game::ClickButton button, d2bs::game::Position pos, d2bs::game::KeyState state)
        : button(button), pos(pos), state(state) {}
    const d2bs::game::ClickButton button;
    const d2bs::game::Position pos;
    const d2bs::game::KeyState state;
    [[nodiscard]] std::string_view Name() const override { return "mouseclick"; }
};

/// @event Mouse moved.
/// @param x {number} - screen x
/// @param y {number} - screen y
class MouseMoveEvent : public BaseEvent {
   protected:
    // JS arg shape unchanged: (x:number, y:number).
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {d2bs::api::v8_convert::ToV8(isolate, pos.x), d2bs::api::v8_convert::ToV8(isolate, pos.y)};
    }

   public:
    explicit MouseMoveEvent(d2bs::game::Position pos) : pos(pos) {}
    const d2bs::game::Position pos;
    [[nodiscard]] std::string_view Name() const override { return "mousemove"; }
};

// ============================================================================
// Communication Events
// ============================================================================

/// @event A chat message was received (non-blocking).
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
class ChatEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, sender),
            d2bs::api::v8_convert::ToV8(isolate, message),
        };
    }

   public:
    ChatEvent(std::string sender, std::string message) : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    [[nodiscard]] std::string_view Name() const override { return "chatmsg"; }
};

/// @event A chat message was received.
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
/// @returns {boolean} - return true to block the message
class ChatBlockerEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, sender),
            d2bs::api::v8_convert::ToV8(isolate, message),
        };
    }

   public:
    ChatBlockerEvent(std::string sender, std::string message)
        : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    [[nodiscard]] std::string_view Name() const override { return "chatmsgblocker"; }
};

/// @event The local player submitted a chat line.
/// @param me {string} - the literal string "me"
/// @param msg {string} - the submitted text
class ChatInputEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, "me"),
            d2bs::api::v8_convert::ToV8(isolate, message),
        };
    }

   public:
    explicit ChatInputEvent(std::string message) : message(std::move(message)) {}
    const std::string message;
    [[nodiscard]] std::string_view Name() const override { return "chatinput"; }
};

/// @event The local player submitted a chat line.
/// @param me {string} - the literal string "me"
/// @param msg {string} - the submitted text
/// @returns {boolean} - return true to block the message
class ChatInputBlockerEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, "me"),
            d2bs::api::v8_convert::ToV8(isolate, message),
        };
    }

   public:
    explicit ChatInputBlockerEvent(std::string message) : message(std::move(message)) {}
    const std::string message;
    [[nodiscard]] std::string_view Name() const override { return "chatinputblocker"; }
};

/// @event A whisper was received (non-blocking).
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
class WhisperEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, sender),
            d2bs::api::v8_convert::ToV8(isolate, message),
        };
    }

   public:
    WhisperEvent(std::string sender, std::string message) : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    [[nodiscard]] std::string_view Name() const override { return "whispermsg"; }
};

/// @event A whisper was received.
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
/// @returns {boolean} - return true to block the message
class WhisperBlockerEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, sender),
            d2bs::api::v8_convert::ToV8(isolate, message),
        };
    }

   public:
    WhisperBlockerEvent(std::string sender, std::string message)
        : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    [[nodiscard]] std::string_view Name() const override { return "whispermsgblocker"; }
};

// ============================================================================
// Network Events
// ============================================================================

/// @event A game packet was received from the server.
/// @param packet {Uint8Array} - the raw packet bytes
/// @returns {boolean} - return true to block the packet
class GamePacketEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        auto arrayBuffer = v8::ArrayBuffer::New(isolate, data.size());
        std::copy_n(data.data(), data.size(), static_cast<uint8_t*>(arrayBuffer->GetBackingStore()->Data()));
        return {v8::Uint8Array::New(arrayBuffer, 0, data.size())};
    }

   public:
    explicit GamePacketEvent(std::span<const uint8_t> data) : data(data.begin(), data.end()) {}
    const std::vector<uint8_t> data;
    [[nodiscard]] std::string_view Name() const override { return "gamepacket"; }
};

/// @event A game packet is about to be sent to the server.
/// @param packet {Uint8Array} - the raw packet bytes
/// @returns {boolean} - return true to block the packet
class GamePacketSentEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        auto arrayBuffer = v8::ArrayBuffer::New(isolate, data.size());
        std::copy_n(data.data(), data.size(), static_cast<uint8_t*>(arrayBuffer->GetBackingStore()->Data()));
        return {v8::Uint8Array::New(arrayBuffer, 0, data.size())};
    }

   public:
    explicit GamePacketSentEvent(std::span<const uint8_t> data) : data(data.begin(), data.end()) {}
    const std::vector<uint8_t> data;
    [[nodiscard]] std::string_view Name() const override { return "gamepacketsent"; }
};

/// @event A realm (BNCS) packet was received.
/// @param packet {Uint8Array} - the raw packet bytes
/// @returns {boolean} - return true to block the packet
class RealmPacketEvent : public BlockableEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        auto arrayBuffer = v8::ArrayBuffer::New(isolate, data.size());
        std::copy_n(data.data(), data.size(), static_cast<uint8_t*>(arrayBuffer->GetBackingStore()->Data()));
        return {v8::Uint8Array::New(arrayBuffer, 0, data.size())};
    }

   public:
    explicit RealmPacketEvent(std::span<const uint8_t> data) : data(data.begin(), data.end()) {}
    const std::vector<uint8_t> data;
    [[nodiscard]] std::string_view Name() const override { return "realmpacket"; }
};

// ============================================================================
// Game Action Events
// ============================================================================

/// @event A game lifecycle/roster event (packet 0x5A: player join/leave/relation).
/// @param mode {number} - the roster event mode
/// @param param1 {number} - first mode-specific parameter
/// @param param2 {number} - second mode-specific parameter
/// @param name1 {string} - first name (e.g. the affected player)
/// @param name2 {string} - second name (e.g. the related player)
class GameActionEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, mode),   d2bs::api::v8_convert::ToV8(isolate, param1),
            d2bs::api::v8_convert::ToV8(isolate, param2), d2bs::api::v8_convert::ToV8(isolate, name1),
            d2bs::api::v8_convert::ToV8(isolate, name2),
        };
    }

   public:
    GameActionEvent(int32_t mode, uint32_t param1, uint32_t param2, std::string name1, std::string name2)
        : mode(mode), param1(param1), param2(param2), name1(std::move(name1)), name2(std::move(name2)) {}
    const int32_t mode;
    const uint32_t param1;
    const uint32_t param2;
    const std::string name1;
    const std::string name2;
    [[nodiscard]] std::string_view Name() const override { return "gameevent"; }
};

/// @event An item add/remove/move action.
/// @param unitId {number} - the affected unit id
/// @param mode {number} - the action mode
/// @param code {string} - the item code
/// @param isGlobal {boolean} - true for the global (0x9D) variant
class ItemActionEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            d2bs::api::v8_convert::ToV8(isolate, unitId),
            d2bs::api::v8_convert::ToV8(isolate, action),
            d2bs::api::v8_convert::ToV8(isolate, code),
            d2bs::api::v8_convert::ToV8(isolate, isGlobal),
        };
    }

   public:
    ItemActionEvent(uint32_t unitId, uint32_t action, std::string code, bool isGlobal)
        : unitId(unitId), action(action), code(std::move(code)), isGlobal(isGlobal) {}
    const uint32_t unitId;
    const uint32_t action;
    const std::string code;
    const bool isGlobal;
    [[nodiscard]] std::string_view Name() const override { return "itemaction"; }
};

/// @event A WM_COPYDATA IPC message from another instance (reserved internal modes are not delivered).
/// @param mode {number} - the IPC mode
/// @param payload {string} - the message payload string
class CopyDataEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {
            // Scripts see the raw integer mode (matches reference CopyDataEvent surface).
            d2bs::api::v8_convert::ToV8(isolate, static_cast<uint32_t>(mode)),
            d2bs::api::v8_convert::ToV8(isolate, payload),
        };
    }

   public:
    CopyDataEvent(d2bs::game::IpcMode mode, std::string payload) : mode(mode), payload(std::move(payload)) {}
    const d2bs::game::IpcMode mode;
    const std::string payload;
    [[nodiscard]] std::string_view Name() const override { return "copydata"; }
};

// ============================================================================
// Script IPC Event
// ============================================================================

/// @event Another script called scriptBroadcast(...) or this script's send(...); non-serializable values arrive as
/// undefined.
/// @param ...args {any} - the delivered values
class BroadcastEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        auto cx = isolate->GetCurrentContext();
        std::vector<v8::Local<v8::Value>> deserializedArgs;
        for (const auto& arg : values_) {
            auto deserializer = v8::ValueDeserializer(isolate, arg.data(), arg.size());
            if (deserializer.ReadHeader(cx).FromMaybe(false)) {
                v8::Local<v8::Value> jsArg;
                if (deserializer.ReadValue(cx).ToLocal(&jsArg)) {
                    deserializedArgs.push_back(jsArg);
                } else {
                    GetLogger(isolate)->critical("Failed to deserialize broadcast event argument");
                }
            } else {
                GetLogger(isolate)->critical("Failed to read broadcast event header");
            }
        }
        return deserializedArgs;
    }

   public:
    explicit BroadcastEvent(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto* isolate = args.GetIsolate();
        values_.reserve(args.Length());
        for (int32_t i = 0; i < args.Length(); i++) {
            v8::ValueSerializer serializer(isolate);
            serializer.WriteHeader();
            if (!serializer.WriteValue(isolate->GetCurrentContext(), args[i]).FromMaybe(false)) {
                // Non-serializable value (function, symbol, etc.) - insert undefined
                // as placeholder to preserve argument positions.
                v8::ValueSerializer undefinedSerializer(isolate);
                undefinedSerializer.WriteHeader();
                undefinedSerializer.WriteValue(isolate->GetCurrentContext(), v8::Undefined(isolate)).Check();
                auto [udData, udSize] = undefinedSerializer.Release();
                values_.emplace_back(udData, udData + udSize);
                std::free(udData);  // NOLINT(cppcoreguidelines-no-malloc)
                continue;
            }
            auto [data, size] = serializer.Release();
            values_.emplace_back(data, data + size);
            std::free(data);  // NOLINT(cppcoreguidelines-no-malloc) - V8's ValueSerializer allocates with realloc()
        }
    }

    [[nodiscard]] std::string_view Name() const override { return "scriptmsg"; }

   private:
    std::vector<std::vector<uint8_t>> values_;
};

// ============================================================================
// Console Evaluate Event
// ============================================================================

class EvaluateEvent : public BaseEvent {
   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* /*isolate*/) const override { return {}; }

   public:
    explicit EvaluateEvent(std::string code) : code(std::move(code)) {}
    const std::string code;

    void Execute(v8::Isolate* isolate, const std::vector<v8::Local<v8::Function>>& /*fns*/) override {
        v8::HandleScope scope(isolate);
        v8::TryCatch tryCatch(isolate);

        auto cx = isolate->GetCurrentContext();
        auto src = d2bs::api::v8_convert::ToV8(isolate, code);

        v8::ScriptOrigin origin(d2bs::api::v8_convert::ToV8(isolate, framework::script::COMMAND_LINE_NAME));
        v8::Local<v8::Script> snippet;
        v8::Local<v8::Value> result;
        if (v8::Script::Compile(cx, src, &origin).ToLocal(&snippet) && snippet->Run(cx).ToLocal(&result)) {
            if (!result->IsUndefined()) {
                v8::String::Utf8Value resultStr(isolate, result);
                d2bs::game::console::OnMessage({
                    .source = d2bs::game::console::MessageSource::EvaluateResult,
                    .name = std::string{framework::script::COMMAND_LINE_NAME},
                    .level = d2bs::game::console::MessageLevel::Info,
                    .text = std::string(*resultStr, resultStr.length()),
                });
            }
        }
        if (tryCatch.HasCaught()) {
            auto message = tryCatch.Message();
            if (!message.IsEmpty()) {
                v8::String::Utf8Value errorStr(isolate, message->Get());
                d2bs::game::console::OnMessage({
                    .source = d2bs::game::console::MessageSource::EvaluateResult,
                    .name = std::string{framework::script::COMMAND_LINE_NAME},
                    .level = d2bs::game::console::MessageLevel::Error,
                    .text = std::string(*errorStr, errorStr.length()),
                });
            }
        }
    }

    [[nodiscard]] std::string_view Name() const override { return "Evaluate"; }
};

// ============================================================================
// Screen Hook Events
// ============================================================================

class ScreenHookClickEvent : public BlockableEvent {
   protected:
    // JS arg shape unchanged: (button:number, x:number, y:number).
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {api::v8_convert::ToV8(isolate, static_cast<uint32_t>(button_)), api::v8_convert::ToV8(isolate, pos_.x),
                api::v8_convert::ToV8(isolate, pos_.y)};
    }

   public:
    ScreenHookClickEvent(d2bs::game::ClickButton button, d2bs::game::Point pos, v8::Global<v8::Function> fn)
        : button_(button), pos_(pos), fn_(std::move(fn)) {}

    void Execute(v8::Isolate* isolate, const std::vector<v8::Local<v8::Function>>& /*fns*/) override {
        BlockableEvent::Execute(isolate, {fn_.Get(isolate)});
    }

    [[nodiscard]] std::string_view Name() const override { return "ScreenHookClick"; }

   private:
    d2bs::game::ClickButton button_;
    d2bs::game::Point pos_;
    v8::Global<v8::Function> fn_;
};

class ScreenHookHoverEvent : public BaseEvent {
   protected:
    // JS arg shape unchanged: (x:number, y:number, entered:bool).  `entered` is
    // emitted as raw bool - documented JS API contract for the screen hook
    // hover callback, even after the bool->enum sweep of other fields.
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const override {
        return {api::v8_convert::ToV8(isolate, pos_.x), api::v8_convert::ToV8(isolate, pos_.y),
                api::v8_convert::ToV8(isolate, entered_)};
    }

   public:
    ScreenHookHoverEvent(d2bs::game::Point pos, bool entered, v8::Global<v8::Function> fn)
        : pos_(pos), entered_(entered), fn_(std::move(fn)) {}

    void Execute(v8::Isolate* isolate, const std::vector<v8::Local<v8::Function>>& /*fns*/) override {
        BaseEvent::Execute(isolate, {fn_.Get(isolate)});
    }

    [[nodiscard]] std::string_view Name() const override { return "ScreenHookHover"; }

   private:
    d2bs::game::Point pos_;
    bool entered_;
    v8::Global<v8::Function> fn_;
};

}  // namespace d2bs
