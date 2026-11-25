#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class DropGoldPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x50;
    static constexpr std::string_view NAME = "DropGold";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    int32_t gold = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        gold = reader.ReadInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, gold={})", Name(), Id(), Size(), unitId, gold);
    }
};

static_assert(sizeof(D2GSPacketClt50) == DropGoldPacket::SIZE);

}  // namespace d2bs::lod114d::packets
