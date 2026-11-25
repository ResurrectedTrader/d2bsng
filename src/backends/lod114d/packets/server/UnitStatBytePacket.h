#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitStatBytePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x9e;
    static constexpr std::string_view NAME = "UnitStatByte";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Stat stat = {};
    uint32_t unitId = {};
    uint8_t value = {};

    void Deserialize(BinaryReader& reader) override {
        stat = static_cast<game::Stat>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        value = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](stat={}, unitId={:#x}, value={})", Name(), Id(), Size(), stat, unitId, value);
    }
};

static_assert(sizeof(D2GSPacketSrv9E) == UnitStatBytePacket::SIZE);

}  // namespace d2bs::lod114d::packets
