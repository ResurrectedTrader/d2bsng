#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterHitPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x0c;
    static constexpr std::string_view NAME = "MonsterHit";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t message = {};
    uint8_t hitClass = {};
    uint8_t life = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        hitClass = reader.ReadByte();
        life = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, message={}, hitClass={}, life={})", Name(), Id(),
                           Size(), unitType, unitId, message, hitClass, life);
    }
};

static_assert(sizeof(D2GSPacketSrv0C) == MonsterHitPacket::SIZE);

}  // namespace d2bs::lod114d::packets
