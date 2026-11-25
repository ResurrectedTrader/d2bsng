#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ItemToCubePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x2a;
    static constexpr std::string_view NAME = "ItemToCube";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t itemId = {};
    uint32_t cubeId = {};

    void Deserialize(BinaryReader& reader) override {
        itemId = reader.ReadUInt32();
        cubeId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](itemId={:#x}, cubeId={:#x})", Name(), Id(), Size(), itemId, cubeId);
    }
};

static_assert(sizeof(D2GSPacketClt2A) == ItemToCubePacket::SIZE);

}  // namespace d2bs::lod114d::packets
