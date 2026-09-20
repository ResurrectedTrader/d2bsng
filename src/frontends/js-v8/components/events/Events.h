#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "BaseEvent.h"
#include "BlockableEvent.h"
#include "components/drawing/Drawable.h"
#include "components/script/ScriptTypes.h"
#include "game/Types.h"

namespace d2bs {

// EVENT_NAME is the one place each event's script-visible name is written: Name() returns it, the
// dispatcher probes it via the type, and scripts/extract_api.py reads it for the API docs.

// ============================================================================
// Game State Events
// ============================================================================

/// @event Player current life (HP) changed, or first observed.
/// @param life {number} - the player's current life (HP)
class LifeEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({life}); }

   public:
    explicit LifeEvent(uint32_t life) : life(life) {}
    const uint32_t life;
    static constexpr std::string_view EVENT_NAME = "melife";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event Player mana changed or first observed.
/// @param mana {number} - the player's current mana
class ManaEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({mana}); }

   public:
    explicit ManaEvent(uint32_t mana) : mana(mana) {}
    const uint32_t mana;
    static constexpr std::string_view EVENT_NAME = "memana";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A player unit id became current or changed.
/// @param unitId {number} - the assigned player unit id
class PlayerAssignEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({unitId}); }

   public:
    explicit PlayerAssignEvent(uint32_t unitId) : unitId(unitId) {}
    const uint32_t unitId;
    static constexpr std::string_view EVENT_NAME = "playerassign";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

// ============================================================================
// Input Events
// ============================================================================

/// @event A key was pressed (non-blocking notification).
/// @param key {number} - the virtual key code
class KeyDownEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({key}); }

   public:
    explicit KeyDownEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    static constexpr std::string_view EVENT_NAME = "keydown";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A key was released (non-blocking notification).
/// @param key {number} - the virtual key code
class KeyUpEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({key}); }

   public:
    explicit KeyUpEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    static constexpr std::string_view EVENT_NAME = "keyup";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A key was pressed.
/// @param key {number} - the virtual key code
/// @returns {boolean} - return true to block the key from the game
class KeyDownBlockerEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({key}); }

   public:
    explicit KeyDownBlockerEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    static constexpr std::string_view EVENT_NAME = "keydownblocker";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A key was released.
/// @param key {number} - the virtual key code
/// @returns {boolean} - return true to block the key from the game
class KeyUpBlockerEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({key}); }

   public:
    explicit KeyUpBlockerEvent(uint32_t key) : key(key) {}
    const uint32_t key;
    static constexpr std::string_view EVENT_NAME = "keyupblocker";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event Mouse button changed.
/// @param button {number} - the mouse button
/// @param x {number} - screen x
/// @param y {number} - screen y
/// @param up {number} - 1 on release, 0 on press
class MouseClickEvent : public BaseEvent {
   protected:
    // JS arg shape unchanged: (button:number, x:number, y:number, up:number 0/1).
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            static_cast<uint32_t>(button),
            pos.x,
            pos.y,
            state == game::KeyState::Up ? 1 : 0,
        });
    }

   public:
    MouseClickEvent(game::ClickButton button, game::Position pos, game::KeyState state)
        : button(button), pos(pos), state(state) {}
    const game::ClickButton button;
    const game::Position pos;
    const game::KeyState state;
    static constexpr std::string_view EVENT_NAME = "mouseclick";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event Mouse moved.
/// @param x {number} - screen x
/// @param y {number} - screen y
class MouseMoveEvent : public BaseEvent {
   protected:
    // JS arg shape unchanged: (x:number, y:number).
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({pos.x, pos.y}); }

   public:
    explicit MouseMoveEvent(game::Position pos) : pos(pos) {}
    const game::Position pos;
    static constexpr std::string_view EVENT_NAME = "mousemove";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

// ============================================================================
// Communication Events
// ============================================================================

/// @event A chat message was received (non-blocking).
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
class ChatEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            sender,
            message,
        });
    }

   public:
    ChatEvent(std::string sender, std::string message) : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    static constexpr std::string_view EVENT_NAME = "chatmsg";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A chat message was received.
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
/// @returns {boolean} - return true to block the message
class ChatBlockerEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            sender,
            message,
        });
    }

   public:
    ChatBlockerEvent(std::string sender, std::string message)
        : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    static constexpr std::string_view EVENT_NAME = "chatmsgblocker";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event The local player submitted a chat line.
/// @param me {string} - the literal string "me"
/// @param msg {string} - the submitted text
class ChatInputEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            "me",
            message,
        });
    }

   public:
    explicit ChatInputEvent(std::string message) : message(std::move(message)) {}
    const std::string message;
    static constexpr std::string_view EVENT_NAME = "chatinput";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event The local player submitted a chat line.
/// @param me {string} - the literal string "me"
/// @param msg {string} - the submitted text
/// @returns {boolean} - return true to block the message
class ChatInputBlockerEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            "me",
            message,
        });
    }

   public:
    explicit ChatInputBlockerEvent(std::string message) : message(std::move(message)) {}
    const std::string message;
    static constexpr std::string_view EVENT_NAME = "chatinputblocker";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A whisper was received (non-blocking).
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
class WhisperEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            sender,
            message,
        });
    }

   public:
    WhisperEvent(std::string sender, std::string message) : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    static constexpr std::string_view EVENT_NAME = "whispermsg";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A whisper was received.
/// @param nick {string} - the sender's name
/// @param msg {string} - the message text
/// @returns {boolean} - return true to block the message
class WhisperBlockerEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            sender,
            message,
        });
    }

   public:
    WhisperBlockerEvent(std::string sender, std::string message)
        : sender(std::move(sender)), message(std::move(message)) {}
    const std::string sender;
    const std::string message;
    static constexpr std::string_view EVENT_NAME = "whispermsgblocker";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

// ============================================================================
// Network Events
// ============================================================================

/// @event A game packet was received from the server.
/// @param packet {Uint8Array} - the raw packet bytes
/// @returns {boolean} - return true to block the packet
class GamePacketEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({js::script::Bytes{.data = data}}); }

   public:
    explicit GamePacketEvent(std::span<const uint8_t> data) : data(data.begin(), data.end()) {}
    const std::vector<uint8_t> data;
    static constexpr std::string_view EVENT_NAME = "gamepacket";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A game packet is about to be sent to the server.
/// @param packet {Uint8Array} - the raw packet bytes
/// @returns {boolean} - return true to block the packet
class GamePacketSentEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({js::script::Bytes{.data = data}}); }

   public:
    explicit GamePacketSentEvent(std::span<const uint8_t> data) : data(data.begin(), data.end()) {}
    const std::vector<uint8_t> data;
    static constexpr std::string_view EVENT_NAME = "gamepacketsent";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A realm (BNCS) packet was received.
/// @param packet {Uint8Array} - the raw packet bytes
/// @returns {boolean} - return true to block the packet
class RealmPacketEvent : public BlockableEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({js::script::Bytes{.data = data}}); }

   public:
    explicit RealmPacketEvent(std::span<const uint8_t> data) : data(data.begin(), data.end()) {}
    const std::vector<uint8_t> data;
    static constexpr std::string_view EVENT_NAME = "realmpacket";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
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
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            mode,
            param1,
            param2,
            name1,
            name2,
        });
    }

   public:
    GameActionEvent(int32_t mode, uint32_t param1, uint32_t param2, std::string name1, std::string name2)
        : mode(mode), param1(param1), param2(param2), name1(std::move(name1)), name2(std::move(name2)) {}
    const int32_t mode;
    const uint32_t param1;
    const uint32_t param2;
    const std::string name1;
    const std::string name2;
    static constexpr std::string_view EVENT_NAME = "gameevent";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event An item add/remove/move action.
/// @param unitId {number} - the affected unit id
/// @param mode {number} - the action mode
/// @param code {string} - the item code
/// @param isGlobal {boolean} - true for the global (0x9D) variant
class ItemActionEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            unitId,
            action,
            code,
            isGlobal,
        });
    }

   public:
    ItemActionEvent(uint32_t unitId, uint32_t action, std::string code, bool isGlobal)
        : unitId(unitId), action(action), code(std::move(code)), isGlobal(isGlobal) {}
    const uint32_t unitId;
    const uint32_t action;
    const std::string code;
    const bool isGlobal;
    static constexpr std::string_view EVENT_NAME = "itemaction";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

/// @event A WM_COPYDATA IPC message from another instance (reserved internal modes are not delivered).
/// @param mode {number} - the IPC mode
/// @param payload {string} - the message payload string
class CopyDataEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({
            // Scripts see the raw integer mode (matches reference CopyDataEvent surface).
            static_cast<uint32_t>(mode),
            payload,
        });
    }

   public:
    CopyDataEvent(game::IpcMode mode, std::string payload) : mode(mode), payload(std::move(payload)) {}
    const game::IpcMode mode;
    const std::string payload;
    static constexpr std::string_view EVENT_NAME = "copydata";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

// ============================================================================
// Script IPC Event
// ============================================================================

/// @event Another script called scriptBroadcast(...) or this script's send(...); non-serializable values arrive as
/// undefined.
/// @param ...args {any} - the delivered values
class BroadcastEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& args) const override {
        std::vector<js::script::Value> values;
        values.reserve(values_.size());
        for (const auto& value : values_) {
            values.emplace_back(js::script::Serialized{.data = value});
        }
        args.Set(std::move(values));
    }

   public:
    explicit BroadcastEvent(std::vector<std::vector<uint8_t>> values) : values_(std::move(values)) {}

    static constexpr std::string_view EVENT_NAME = "scriptmsg";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }

   private:
    std::vector<std::vector<uint8_t>> values_;
};

// ============================================================================
// Screen Hook Events
// ============================================================================

// The two events the game thread raises. It picks the drawable and keeps it
// alive, but never the handler: that is looked up on the owning script's thread
// when the event runs, so a handler cleared in between simply does not fire.

class ScreenHookClickEvent : public BlockableEvent {
   protected:
    // (button:number, x:number, y:number).
    void MakeArgs(js::script::CallArgs& args) const override {
        args.Set({static_cast<uint32_t>(button_), pos_.x, pos_.y});
    }

   public:
    ScreenHookClickEvent(std::shared_ptr<const js::drawing::Drawable> drawable, game::ClickButton button,
                         game::Point pos)
        : drawable_(std::move(drawable)), button_(button), pos_(pos) {}

    void Execute(js::script::Invocation& call) override { Vote(call.Run(*this, *drawable_, DrawableHandler::Click)); }

    static constexpr std::string_view EVENT_NAME = "ScreenHookClick";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }

   private:
    std::shared_ptr<const js::drawing::Drawable> drawable_;
    game::ClickButton button_;
    game::Point pos_;
};

class ScreenHookHoverEvent : public BaseEvent {
   protected:
    // (x:number, y:number, entered:bool). `entered` is emitted as a raw bool -
    // documented JS API contract for the screen hook hover callback, even after
    // the bool->enum sweep of other fields.
    void MakeArgs(js::script::CallArgs& args) const override { args.Set({pos_.x, pos_.y, entered_}); }

   public:
    ScreenHookHoverEvent(std::shared_ptr<const js::drawing::Drawable> drawable, game::Point pos, bool entered)
        : drawable_(std::move(drawable)), pos_(pos), entered_(entered) {}

    void Execute(js::script::Invocation& call) override { call.Run(*this, *drawable_, DrawableHandler::Hover); }

    static constexpr std::string_view EVENT_NAME = "ScreenHookHover";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }

   private:
    std::shared_ptr<const js::drawing::Drawable> drawable_;
    game::Point pos_;
    bool entered_;
};

// ============================================================================
// Console Evaluate Event
// ============================================================================

class EvaluateEvent : public BaseEvent {
   protected:
    void MakeArgs(js::script::CallArgs& /*args*/) const override {}

   public:
    explicit EvaluateEvent(std::string code) : code(std::move(code)) {}
    const std::string code;

    void Execute(js::script::Invocation& call) override { call.Evaluate(code); }

    static constexpr std::string_view EVENT_NAME = "Evaluate";
    [[nodiscard]] std::string_view Name() const override { return EVENT_NAME; }
};

}  // namespace d2bs
