#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SwapCursorBufferItemsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x1f;
    static constexpr std::string_view NAME = "SwapCursorBufferItems";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t cursorItemId = {};
    uint32_t inventoryItemId = {};
    Point32 position = {};

    void Deserialize(BinaryReader& reader) override {
        cursorItemId = reader.ReadUInt32();
        inventoryItemId = reader.ReadUInt32();
        position = reader.Read<Point32>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](cursorItemId={:#x}, inventoryItemId={:#x}, position=({}, {}))", Name(), Id(),
                           Size(), cursorItemId, inventoryItemId, position.x, position.y);
    }
};

static_assert(sizeof(D2GSPacketClt1F) == SwapCursorBufferItemsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
