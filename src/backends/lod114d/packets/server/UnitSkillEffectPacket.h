#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitSkillEffectPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xa3;
    static constexpr std::string_view NAME = "UnitSkillEffect";
    static constexpr uint32_t SIZE = 24;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t seed = {};
    game::Skill skillId = {};
    uint16_t skillLevel = {};
    game::UnitType unitType = {};
    uint32_t unitId = {};
    game::UnitType targetType = {};
    uint32_t targetId = {};
    Point32 position = {};

    void Deserialize(BinaryReader& reader) override {
        seed = reader.ReadByte();
        skillId = static_cast<game::Skill>(reader.ReadUInt16());
        skillLevel = reader.ReadUInt16();
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        targetType = static_cast<game::UnitType>(reader.ReadByte());
        targetId = reader.ReadUInt32();
        position = reader.Read<Point32>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](seed={:#x}, skillId={}, skillLevel={}, unitType={}, unitId={:#x}, "
                           "targetType={}, targetId={:#x}, position=({}, {}))",
                           Name(), Id(), Size(), seed, skillId, skillLevel, unitType, unitId, targetType, targetId,
                           position.x, position.y);
    }
};

static_assert(sizeof(D2GSPacketSrvA3) == UnitSkillEffectPacket::SIZE);

}  // namespace d2bs::lod114d::packets
