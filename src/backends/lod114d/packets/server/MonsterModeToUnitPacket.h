#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterModeToUnitPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x6c;
    static constexpr std::string_view NAME = "MonsterModeToUnit";
    static constexpr uint32_t SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint8_t message = {};
    game::UnitType targetUnitType = {};
    uint32_t targetUnitId = {};
    uint8_t unitLife = {};
    Point16 position = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        targetUnitType = static_cast<game::UnitType>(reader.ReadByte());
        targetUnitId = reader.ReadUInt32();
        unitLife = reader.ReadByte();
        position = reader.Read<Point16>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, message={}, targetUnitType={}, targetUnitId={:#x}, "
                           "unitLife={}, position=({}, {}))",
                           Name(), Id(), Size(), unitId, message, targetUnitType, targetUnitId, unitLife, position.x,
                           position.y);
    }
};

static_assert(sizeof(D2GSPacketSrv6C) == MonsterModeToUnitPacket::SIZE);

}  // namespace d2bs::lod114d::packets
