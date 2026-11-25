#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class InsertItemInBufferPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x18;
    static constexpr std::string_view NAME = "InsertItemInBuffer";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t itemId = {};
    Point32 position = {};
    uint32_t page = {};

    void Deserialize(BinaryReader& reader) override {
        itemId = reader.ReadUInt32();
        position = reader.Read<Point32>();
        page = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](itemId={:#x}, position=({}, {}), page={})", Name(), Id(), Size(), itemId,
                           position.x, position.y, page);
    }
};

static_assert(sizeof(D2GSPacketClt18) == InsertItemInBufferPacket::SIZE);

}  // namespace d2bs::lod114d::packets
