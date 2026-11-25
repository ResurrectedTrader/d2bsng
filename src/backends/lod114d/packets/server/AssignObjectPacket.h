#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignObjectPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x51;
    static constexpr std::string_view NAME = "AssignObject";
    static constexpr uint32_t SIZE = 14;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint16_t classId = {};
    Point16 position = {};
    uint8_t objectParam = {};
    uint8_t interactType = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        classId = reader.ReadUInt16();
        position = reader.Read<Point16>();
        objectParam = reader.ReadByte();
        interactType = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitType={}, unitId={:#x}, classId={}, position=({}, {}), objectParam={}, interactType={})",
            Name(), Id(), Size(), unitType, unitId, classId, position.x, position.y, objectParam, interactType);
    }
};

static_assert(sizeof(D2GSPacketSrv51) == AssignObjectPacket::SIZE);

}  // namespace d2bs::lod114d::packets
