#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UseGridItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x20;
    static constexpr std::string_view NAME = "UseGridItem";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t itemId = {};
    Point32 position = {};

    void Deserialize(BinaryReader& reader) override {
        itemId = reader.ReadUInt32();
        position = reader.Read<Point32>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](itemId={:#x}, position=({}, {}))", Name(), Id(), Size(), itemId, position.x,
                           position.y);
    }
};

static_assert(sizeof(D2GSPacketClt20) == UseGridItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
