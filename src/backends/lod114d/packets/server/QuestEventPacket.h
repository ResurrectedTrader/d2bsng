#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestEventPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x89;
    static constexpr std::string_view NAME = "QuestEvent";
    static constexpr uint32_t SIZE = 2;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t event = {};

    void Deserialize(BinaryReader& reader) override { event = reader.ReadByte(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](event={})", Name(), Id(), Size(), event);
    }
};

static_assert(sizeof(D2GSPacketSrv89) == QuestEventPacket::SIZE);

}  // namespace d2bs::lod114d::packets
