#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitMovePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x0f;
    static constexpr std::string_view NAME = "UnitMove";
    static constexpr uint32_t SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t flags = {};
    Point16 destination = {};
    uint8_t hitClass = {};
    Point16 current = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        flags = reader.ReadByte();
        destination = reader.Read<Point16>();
        hitClass = reader.ReadByte();
        current = reader.Read<Point16>();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitType={}, unitId={:#x}, flags={}, destination=({}, {}), hitClass={}, current=({}, {}))",
            Name(), Id(), Size(), unitType, unitId, flags, destination.x, destination.y, hitClass, current.x,
            current.y);
    }
};

static_assert(sizeof(D2GSPacketSrv0F) == UnitMovePacket::SIZE);

}  // namespace d2bs::lod114d::packets
