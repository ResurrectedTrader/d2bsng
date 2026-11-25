#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterModeOnUnitPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x6a;
    static constexpr std::string_view NAME = "MonsterModeOnUnit";
    static constexpr uint32_t SIZE = 12;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint8_t message = {};
    game::UnitType targetUnitType = {};
    uint32_t targetUnitId = {};
    uint8_t direction = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        targetUnitType = static_cast<game::UnitType>(reader.ReadByte());
        targetUnitId = reader.ReadUInt32();
        direction = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitId={:#x}, message={}, targetUnitType={}, targetUnitId={:#x}, direction={})", Name(),
            Id(), Size(), unitId, message, targetUnitType, targetUnitId, direction);
    }
};

static_assert(sizeof(D2GSPacketSrv6A) == MonsterModeOnUnitPacket::SIZE);

}  // namespace d2bs::lod114d::packets
