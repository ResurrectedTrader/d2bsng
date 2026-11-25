#pragma once

#include <array>

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// The dialog lines an NPC offers when an interaction starts (D2GAME_PACKETS_SendPacket0x27).
class NpcMessageListPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x27;
    static constexpr std::string_view NAME = "NpcMessageList";
    static constexpr uint32_t SIZE = 40;
    static constexpr size_t MAX_MESSAGES = 8;

    struct Message {
        uint8_t menu = 0;
        uint16_t stringId = 0;
    };

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = 0;
    uint8_t count = 0;
    std::array<Message, MAX_MESSAGES> messages = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        count = reader.ReadByte();
        reader.ReadByte();
        for (auto& message : messages) {
            message.menu = reader.ReadByte();
            reader.ReadByte();
            message.stringId = reader.ReadUInt16();
        }
    }

    std::string ToString() const override {
        const auto shown = std::span{messages}.first(std::min<size_t>(count, MAX_MESSAGES));
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, messages=[{}])", Name(), Id(), Size(), unitType,
                           unitId, Join(shown, [](const Message& message) {
                               return std::format("(menu={}, stringId={})", message.menu, message.stringId);
                           }));
    }
};

static_assert(sizeof(D2GSPacketSrv27) == NpcMessageListPacket::SIZE);

}  // namespace d2bs::lod114d::packets
