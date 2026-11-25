#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class WaypointMenuPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x63;
    static constexpr std::string_view NAME = "WaypointMenu";
    static constexpr uint32_t SIZE = 21;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    std::array<uint8_t, 16> waypoints;

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        reader.ReadInto(waypoints);
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, waypoints={})", Name(), Id(), Size(), unitId,
                           ToHexString(waypoints));
    }
};

static_assert(sizeof(D2GSPacketSrv63) == WaypointMenuPacket::SIZE);

}  // namespace d2bs::lod114d::packets
