#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class LeftSkillOnUnitHoldPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x09;
    static constexpr std::string_view NAME = "LeftSkillOnUnitHold";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadUInt32());
        unitId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x})", Name(), Id(), Size(), unitType, unitId);
    }
};

static_assert(sizeof(D2GSPacketClt09) == LeftSkillOnUnitHoldPacket::SIZE);

}  // namespace d2bs::lod114d::packets
