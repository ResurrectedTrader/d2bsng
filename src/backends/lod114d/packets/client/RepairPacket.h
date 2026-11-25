#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class RepairPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x35;
    static constexpr std::string_view NAME = "Repair";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t npcId = {};
    uint32_t itemId = {};
    int32_t unk0x09 = {};
    int32_t unk0x0D = {};

    void Deserialize(BinaryReader& reader) override {
        npcId = reader.ReadUInt32();
        itemId = reader.ReadUInt32();
        unk0x09 = reader.ReadInt32();
        unk0x0D = reader.ReadInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](npcId={:#x}, itemId={:#x}, unk0x09={}, unk0x0D={})", Name(), Id(), Size(),
                           npcId, itemId, unk0x09, unk0x0D);
    }
};

static_assert(sizeof(D2GSPacketClt35) == RepairPacket::SIZE);

}  // namespace d2bs::lod114d::packets
