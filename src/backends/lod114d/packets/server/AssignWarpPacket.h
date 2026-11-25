#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignWarpPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x09;
    static constexpr std::string_view NAME = "AssignWarp";
    static constexpr uint32_t SIZE = 11;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t classId = {};
    Point16 position = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        classId = reader.ReadByte();
        position = reader.Read<Point16>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, classId={}, position=({}, {}))", Name(), Id(),
                           Size(), unitType, unitId, classId, position.x, position.y);
    }
};

static_assert(sizeof(D2GSPacketSrv09) == AssignWarpPacket::SIZE);

}  // namespace d2bs::lod114d::packets
