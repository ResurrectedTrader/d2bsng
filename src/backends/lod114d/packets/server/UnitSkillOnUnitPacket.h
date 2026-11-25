#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitSkillOnUnitPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x99;
    static constexpr std::string_view NAME = "UnitSkillOnUnit";
    static constexpr uint32_t SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType sourceUnitType = {};
    uint32_t sourceUnitId = {};
    game::Skill skillId = {};
    uint8_t skillLevel = {};
    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint16_t itemEffect = {};

    void Deserialize(BinaryReader& reader) override {
        sourceUnitType = static_cast<game::UnitType>(reader.ReadByte());
        sourceUnitId = reader.ReadUInt32();
        skillId = static_cast<game::Skill>(reader.ReadUInt16());
        skillLevel = reader.ReadByte();
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        itemEffect = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](sourceUnitType={}, sourceUnitId={:#x}, skillId={}, skillLevel={}, "
                           "unitType={}, unitId={:#x}, itemEffect={})",
                           Name(), Id(), Size(), sourceUnitType, sourceUnitId, skillId, skillLevel, unitType, unitId,
                           itemEffect);
    }
};

static_assert(sizeof(D2GSPacketSrv99) == UnitSkillOnUnitPacket::SIZE);

}  // namespace d2bs::lod114d::packets
