#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MakeEntityMovePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x59;
    static constexpr std::string_view NAME = "MakeEntityMove";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    Point32 target = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadUInt32());
        unitId = reader.ReadUInt32();
        target = reader.Read<Point32>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, target=({}, {}))", Name(), Id(), Size(), unitType,
                           unitId, target.x, target.y);
    }
};

static_assert(sizeof(D2GSPacketClt59) == MakeEntityMovePacket::SIZE);

}  // namespace d2bs::lod114d::packets
