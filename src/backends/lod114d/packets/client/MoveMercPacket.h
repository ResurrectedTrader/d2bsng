#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MoveMercPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x47;
    static constexpr std::string_view NAME = "MoveMerc";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t mercId = {};
    Point32 position = {};

    void Deserialize(BinaryReader& reader) override {
        mercId = reader.ReadUInt32();
        position = reader.Read<Point32>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](mercId={:#x}, position=({}, {}))", Name(), Id(), Size(), mercId, position.x,
                           position.y);
    }
};

static_assert(sizeof(D2GSPacketClt47) == MoveMercPacket::SIZE);

}  // namespace d2bs::lod114d::packets
