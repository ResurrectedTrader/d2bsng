#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitCastAtLocationPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x4d;
    static constexpr std::string_view NAME = "UnitCastAtLocation";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    game::Skill skillId = {};
    uint8_t skillLevel = {};
    Point16 position = {};
    uint16_t itemEffect = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        skillId = static_cast<game::Skill>(reader.ReadUInt32());
        skillLevel = reader.ReadByte();
        position = reader.Read<Point16>();
        itemEffect = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitType={}, unitId={:#x}, skillId={}, skillLevel={}, position=({}, {}), itemEffect={})",
            Name(), Id(), Size(), unitType, unitId, skillId, skillLevel, position.x, position.y, itemEffect);
    }
};

static_assert(sizeof(D2GSPacketSrv4D) == UnitCastAtLocationPacket::SIZE);

}  // namespace d2bs::lod114d::packets
