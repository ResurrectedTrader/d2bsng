#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestCompletedPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x58;
    static constexpr std::string_view NAME = "QuestCompleted";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t quest = {};

    void Deserialize(BinaryReader& reader) override { quest = reader.ReadUInt16(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](quest={})", Name(), Id(), Size(), quest);
    }
};

static_assert(sizeof(D2GSPacketClt58) == QuestCompletedPacket::SIZE);

}  // namespace d2bs::lod114d::packets
