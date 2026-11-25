#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SellItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x33;
    static constexpr std::string_view NAME = "SellItem";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t npcId = {};
    uint32_t itemId = {};
    uint16_t tab = {};
    uint16_t unk0x0B = {};
    uint32_t gold = {};

    void Deserialize(BinaryReader& reader) override {
        npcId = reader.ReadUInt32();
        itemId = reader.ReadUInt32();
        tab = reader.ReadUInt16();
        unk0x0B = reader.ReadUInt16();
        gold = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](npcId={:#x}, itemId={:#x}, tab={}, unk0x0B={}, gold={})", Name(), Id(),
                           Size(), npcId, itemId, tab, unk0x0B, gold);
    }
};

static_assert(sizeof(D2GSPacketClt33) == SellItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
