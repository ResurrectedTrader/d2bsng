#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitStatPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x20;
    static constexpr std::string_view NAME = "UnitStat";
    static constexpr uint32_t SIZE = 10;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    game::Stat stat = {};
    int32_t value = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        stat = static_cast<game::Stat>(reader.ReadByte());
        value = reader.ReadInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, stat={}, value={})", Name(), Id(), Size(), unitId, stat, value);
    }
};

static_assert(sizeof(D2GSPacketSrv20) == UnitStatPacket::SIZE);

}  // namespace d2bs::lod114d::packets
