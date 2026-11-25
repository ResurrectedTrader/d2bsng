#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlayNpcMessagePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x4d;
    static constexpr std::string_view NAME = "PlayNpcMessage";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t monStatsId = {};

    void Deserialize(BinaryReader& reader) override { monStatsId = reader.ReadUInt16(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](monStatsId={})", Name(), Id(), Size(), monStatsId);
    }
};

static_assert(sizeof(D2GSPacketClt4D) == PlayNpcMessagePacket::SIZE);

}  // namespace d2bs::lod114d::packets
