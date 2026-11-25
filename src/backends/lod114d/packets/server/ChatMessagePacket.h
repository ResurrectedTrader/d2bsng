#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A chat line, whisper, overhead text or server notice (D2GAME_PACKETS_SendPacket0x26_ServerMessage): two
// NUL-terminated strings after a fixed head.
class ChatMessagePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x26;
    static constexpr std::string_view NAME = "ChatMessage";

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return size_; }
    bool IsVariableSize() const override { return true; }

    uint8_t messageType = 0;
    uint8_t language = 0;
    game::UnitType unitType = {};
    uint32_t unitId = 0;
    uint8_t messageColor = 0;
    uint8_t nameColor = 0;
    std::string name;
    std::string message;

    void Deserialize(BinaryReader& reader) override {
        messageType = reader.ReadByte();
        language = reader.ReadByte();
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        messageColor = reader.ReadByte();
        nameColor = reader.ReadByte();
        name = reader.ReadString();
        message = reader.ReadString();
        size_ = reader.Position() + 1;
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](messageType={}, language={}, unitType={}, unitId={:#x}, messageColor={}, nameColor={}, "
            "name={}, message={})",
            Name(), Id(), Size(), messageType, language, unitType, unitId, messageColor, nameColor, name, message);
    }

   private:
    uint32_t size_ = 0;
};

}  // namespace d2bs::lod114d::packets
