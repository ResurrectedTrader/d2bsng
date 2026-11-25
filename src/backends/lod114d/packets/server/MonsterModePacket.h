#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterModePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x69;
    static constexpr std::string_view NAME = "MonsterMode";
    static constexpr uint32_t SIZE = 12;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint8_t message = {};
    Point16 position = {};
    uint8_t unitLife = {};
    uint8_t hitClass = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        position = reader.Read<Point16>();
        unitLife = reader.ReadByte();
        hitClass = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, message={}, position=({}, {}), unitLife={}, hitClass={})",
                           Name(), Id(), Size(), unitId, message, position.x, position.y, unitLife, hitClass);
    }
};

static_assert(sizeof(D2GSPacketSrv69) == MonsterModePacket::SIZE);

}  // namespace d2bs::lod114d::packets
