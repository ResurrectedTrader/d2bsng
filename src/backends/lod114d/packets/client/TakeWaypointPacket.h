#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class TakeWaypointPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x49;
    static constexpr std::string_view NAME = "TakeWaypoint";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t waypointId = {};
    game::LevelId levelId = {};

    void Deserialize(BinaryReader& reader) override {
        waypointId = reader.ReadUInt32();
        levelId = static_cast<game::LevelId>(reader.ReadInt32());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](waypointId={:#x}, levelId={})", Name(), Id(), Size(), waypointId, levelId);
    }
};

static_assert(sizeof(D2GSPacketClt49) == TakeWaypointPacket::SIZE);

}  // namespace d2bs::lod114d::packets
