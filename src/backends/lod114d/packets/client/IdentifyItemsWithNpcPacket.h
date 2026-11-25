#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class IdentifyItemsWithNpcPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x34;
    static constexpr std::string_view NAME = "IdentifyItemsWithNpc";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t npcId = {};

    void Deserialize(BinaryReader& reader) override { npcId = reader.ReadUInt32(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](npcId={:#x})", Name(), Id(), Size(), npcId);
    }
};

static_assert(sizeof(D2GSPacketClt34) == IdentifyItemsWithNpcPacket::SIZE);

}  // namespace d2bs::lod114d::packets
