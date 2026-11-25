#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterMovePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x67;
    static constexpr std::string_view NAME = "MonsterMove";
    static constexpr uint32_t SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint8_t message = {};
    Point16 position = {};
    int8_t unitLife = {};
    int8_t hitClass = {};
    int8_t pathType = {};
    int16_t velocity = {};
    int8_t maxDistance = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        position = reader.Read<Point16>();
        unitLife = reader.Read<int8_t>();
        hitClass = reader.Read<int8_t>();
        pathType = reader.Read<int8_t>();
        velocity = reader.ReadInt16();
        maxDistance = reader.Read<int8_t>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, message={}, position=({}, {}), unitLife={}, hitClass={}, "
                           "pathType={}, velocity={}, maxDistance={})",
                           Name(), Id(), Size(), unitId, message, position.x, position.y, unitLife, hitClass, pathType,
                           velocity, maxDistance);
    }
};

static_assert(sizeof(D2GSPacketSrv67) == MonsterMovePacket::SIZE);

}  // namespace d2bs::lod114d::packets
