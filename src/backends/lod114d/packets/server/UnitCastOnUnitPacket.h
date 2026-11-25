#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitCastOnUnitPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x4c;
    static constexpr std::string_view NAME = "UnitCastOnUnit";
    static constexpr uint32_t SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    game::Skill skillId = {};
    uint8_t skillLevel = {};
    game::UnitType targetType = {};
    uint32_t targetId = {};
    uint16_t itemEffect = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        skillId = static_cast<game::Skill>(reader.ReadUInt16());
        skillLevel = reader.ReadByte();
        targetType = static_cast<game::UnitType>(reader.ReadByte());
        targetId = reader.ReadUInt32();
        itemEffect = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, skillId={}, skillLevel={}, targetType={}, "
                           "targetId={:#x}, itemEffect={})",
                           Name(), Id(), Size(), unitType, unitId, skillId, skillLevel, targetType, targetId,
                           itemEffect);
    }
};

static_assert(sizeof(D2GSPacketSrv4C) == UnitCastOnUnitPacket::SIZE);

}  // namespace d2bs::lod114d::packets
