#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnstackItemsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x22;
    static constexpr std::string_view NAME = "UnstackItems";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t itemId = {};

    void Deserialize(BinaryReader& reader) override { itemId = reader.ReadUInt32(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](itemId={:#x})", Name(), Id(), Size(), itemId);
    }
};

static_assert(sizeof(D2GSPacketClt22) == UnstackItemsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
