#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterModeAtLocationPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x6b;
    static constexpr std::string_view NAME = "MonsterModeAtLocation";
    static constexpr uint32_t SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint8_t message = {};
    Point16 target = {};
    uint8_t unitLife = {};
    uint8_t hitClass = {};
    Point16 position = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        target = reader.Read<Point16>();
        unitLife = reader.ReadByte();
        hitClass = reader.ReadByte();
        position = reader.Read<Point16>();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitId={:#x}, message={}, target=({}, {}), unitLife={}, hitClass={}, position=({}, {}))",
            Name(), Id(), Size(), unitId, message, target.x, target.y, unitLife, hitClass, position.x, position.y);
    }
};

static_assert(sizeof(D2GSPacketSrv6B) == MonsterModeAtLocationPacket::SIZE);

}  // namespace d2bs::lod114d::packets
