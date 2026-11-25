#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterStopPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x6d;
    static constexpr std::string_view NAME = "MonsterStop";
    static constexpr uint32_t SIZE = 10;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    Point16 position = {};
    uint8_t unitLife = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        position = reader.Read<Point16>();
        unitLife = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, position=({}, {}), unitLife={})", Name(), Id(), Size(), unitId,
                           position.x, position.y, unitLife);
    }
};

static_assert(sizeof(D2GSPacketSrv6D) == MonsterStopPacket::SIZE);

}  // namespace d2bs::lod114d::packets
