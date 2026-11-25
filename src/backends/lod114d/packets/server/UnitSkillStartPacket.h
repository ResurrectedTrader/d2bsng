#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitSkillStartPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xa5;
    static constexpr std::string_view NAME = "UnitSkillStart";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    game::Skill skillId = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        skillId = static_cast<game::Skill>(reader.ReadUInt16());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, skillId={})", Name(), Id(), Size(), unitType,
                           unitId, skillId);
    }
};

static_assert(sizeof(D2GSPacketSrvA5) == UnitSkillStartPacket::SIZE);

}  // namespace d2bs::lod114d::packets
