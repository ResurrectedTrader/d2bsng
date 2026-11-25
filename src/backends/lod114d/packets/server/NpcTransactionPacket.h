#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class NpcTransactionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x2a;
    static constexpr std::string_view NAME = "NpcTransaction";
    static constexpr uint32_t SIZE = 15;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t unk0x01 = {};
    uint8_t result = {};
    uint32_t unk0x03 = {};
    uint32_t unitId = {};
    uint32_t gold = {};

    void Deserialize(BinaryReader& reader) override {
        unk0x01 = reader.ReadByte();
        result = reader.ReadByte();
        unk0x03 = reader.ReadUInt32();
        unitId = reader.ReadUInt32();
        gold = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unk0x01={}, result={}, unk0x03={}, unitId={:#x}, gold={})", Name(), Id(),
                           Size(), unk0x01, result, unk0x03, unitId, gold);
    }
};

static_assert(sizeof(D2GSPacketSrv2A) == NpcTransactionPacket::SIZE);

}  // namespace d2bs::lod114d::packets
