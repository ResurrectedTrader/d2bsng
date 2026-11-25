#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitStopPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x0d;
    static constexpr std::string_view NAME = "UnitStop";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t hitClass = {};
    Point16 position = {};
    uint8_t hitType = {};
    uint8_t unitLifePct = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        hitClass = reader.ReadByte();
        position = reader.Read<Point16>();
        hitType = reader.ReadByte();
        unitLifePct = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitType={}, unitId={:#x}, hitClass={}, position=({}, {}), hitType={}, unitLifePct={})",
            Name(), Id(), Size(), unitType, unitId, hitClass, position.x, position.y, hitType, unitLifePct);
    }
};

static_assert(sizeof(D2GSPacketSrv0D) == UnitStopPacket::SIZE);

}  // namespace d2bs::lod114d::packets
