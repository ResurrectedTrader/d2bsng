#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitWarpPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x15;
    static constexpr std::string_view NAME = "UnitWarp";
    static constexpr uint32_t SIZE = 11;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    Point16 position = {};
    bool reassign = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        position = reader.Read<Point16>();
        reassign = reader.ReadBoolean();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, position=({}, {}), reassign={})", Name(), Id(),
                           Size(), unitType, unitId, position.x, position.y, reassign);
    }
};

static_assert(sizeof(D2GSPacketSrv15) == UnitWarpPacket::SIZE);

}  // namespace d2bs::lod114d::packets
