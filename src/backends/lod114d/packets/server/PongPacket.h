#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PongPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x8f;
    static constexpr std::string_view NAME = "Pong";
    static constexpr uint32_t SIZE = 33;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::array<uint8_t, 32> pongData;

    void Deserialize(BinaryReader& reader) override { reader.ReadInto(pongData); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](pongData={})", Name(), Id(), Size(), ToHexString(pongData));
    }
};

static_assert(sizeof(D2GSPacketSrv8F) == PongPacket::SIZE);

}  // namespace d2bs::lod114d::packets
