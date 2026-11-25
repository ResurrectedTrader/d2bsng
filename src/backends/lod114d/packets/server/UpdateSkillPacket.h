#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UpdateSkillPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x21;
    static constexpr std::string_view NAME = "UpdateSkill";
    static constexpr uint32_t SIZE = 12;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    bool remove = {};
    uint32_t unitId = {};
    game::Skill skillId = {};
    uint8_t skillLevel = {};
    uint8_t bonusSkillLevel = {};
    uint8_t unk0x0B = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        remove = reader.ReadBoolean();
        unitId = reader.ReadUInt32();
        skillId = static_cast<game::Skill>(reader.ReadUInt16());
        skillLevel = reader.ReadByte();
        bonusSkillLevel = reader.ReadByte();
        unk0x0B = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, remove={}, unitId={:#x}, skillId={}, skillLevel={}, "
                           "bonusSkillLevel={}, unk0x0B={})",
                           Name(), Id(), Size(), unitType, remove, unitId, skillId, skillLevel, bonusSkillLevel,
                           unk0x0B);
    }
};

static_assert(sizeof(D2GSPacketSrv21) == UpdateSkillPacket::SIZE);

}  // namespace d2bs::lod114d::packets
