#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitStatWordPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x9f;
    static constexpr std::string_view NAME = "UnitStatWord";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Stat stat = {};
    uint32_t unitId = {};
    uint16_t value = {};

    void Deserialize(BinaryReader& reader) override {
        stat = static_cast<game::Stat>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        value = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](stat={}, unitId={:#x}, value={})", Name(), Id(), Size(), stat, unitId, value);
    }
};

static_assert(sizeof(D2GSPacketSrv9F) == UnitStatWordPacket::SIZE);

}  // namespace d2bs::lod114d::packets
