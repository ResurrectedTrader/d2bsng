#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class RunPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x03;
    static constexpr std::string_view NAME = "Run";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    Point16 position = {};

    void Deserialize(BinaryReader& reader) override { position = reader.Read<Point16>(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](position=({}, {}))", Name(), Id(), Size(), position.x, position.y);
    }
};

static_assert(sizeof(D2GSPacketClt03) == RunPacket::SIZE);

}  // namespace d2bs::lod114d::packets
