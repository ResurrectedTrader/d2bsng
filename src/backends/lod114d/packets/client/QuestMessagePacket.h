#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestMessagePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x31;
    static constexpr std::string_view NAME = "QuestMessage";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t npcId = {};
    uint32_t messageId = {};

    void Deserialize(BinaryReader& reader) override {
        npcId = reader.ReadUInt32();
        messageId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](npcId={:#x}, messageId={:#x})", Name(), Id(), Size(), npcId, messageId);
    }
};

static_assert(sizeof(D2GSPacketClt31) == QuestMessagePacket::SIZE);

}  // namespace d2bs::lod114d::packets
