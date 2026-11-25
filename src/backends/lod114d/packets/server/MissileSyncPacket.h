#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MissileSyncPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x73;
    static constexpr std::string_view NAME = "MissileSync";
    static constexpr uint32_t SIZE = 32;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    int32_t unk0x01 = {};
    int16_t missileId = {};
    Point32 position = {};
    Point32 origin = {};
    int16_t currentFrame = {};
    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t level = {};
    uint8_t pierce = {};

    void Deserialize(BinaryReader& reader) override {
        unk0x01 = reader.ReadInt32();
        missileId = reader.ReadInt16();
        position = reader.Read<Point32>();
        origin = reader.Read<Point32>();
        currentFrame = reader.ReadInt16();
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        level = reader.ReadByte();
        pierce = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unk0x01={}, missileId={}, position=({}, {}), origin=({}, {}), "
                           "currentFrame={}, unitType={}, unitId={:#x}, level={}, pierce={})",
                           Name(), Id(), Size(), unk0x01, missileId, position.x, position.y, origin.x, origin.y,
                           currentFrame, unitType, unitId, level, pierce);
    }
};

static_assert(sizeof(D2GSPacketSrv73) == MissileSyncPacket::SIZE);

}  // namespace d2bs::lod114d::packets
