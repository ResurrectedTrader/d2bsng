#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestFlagsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x29;
    static constexpr std::string_view NAME = "QuestFlags";
    static constexpr uint32_t SIZE = 97;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::array<uint8_t, 96> data;

    void Deserialize(BinaryReader& reader) override { reader.ReadInto(data); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](data={})", Name(), Id(), Size(), ToHexString(data));
    }
};

static_assert(sizeof(D2GSPacketSrv29) == QuestFlagsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
