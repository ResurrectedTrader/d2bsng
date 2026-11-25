#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignSkillPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x23;
    static constexpr std::string_view NAME = "AssignSkill";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t hand = {};
    game::Skill skill = {};
    uint32_t ownerId = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        hand = reader.ReadByte();
        skill = static_cast<game::Skill>(reader.ReadUInt16());
        ownerId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, hand={}, skill={}, ownerId={:#x})", Name(), Id(),
                           Size(), unitType, unitId, hand, skill, ownerId);
    }
};

static_assert(sizeof(D2GSPacketSrv23) == AssignSkillPacket::SIZE);

}  // namespace d2bs::lod114d::packets
