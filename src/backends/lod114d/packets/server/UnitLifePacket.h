#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitLifePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xab;
    static constexpr std::string_view NAME = "UnitLife";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t life = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        life = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, life={})", Name(), Id(), Size(), unitType, unitId,
                           life);
    }
};

static_assert(sizeof(D2GSPacketSrvAB) == UnitLifePacket::SIZE);

}  // namespace d2bs::lod114d::packets
