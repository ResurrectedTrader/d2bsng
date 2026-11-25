#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MonsterUniqueInfoPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x57;
    static constexpr std::string_view NAME = "MonsterUniqueInfo";
    static constexpr uint32_t SIZE = 14;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    game::UnitType unitType = {};
    uint16_t nameSeed = {};
    std::array<uint8_t, 4> uniqueMods;
    bool champion = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        nameSeed = reader.ReadUInt16();
        reader.ReadInto(uniqueMods);
        champion = reader.ReadUInt16() != 0;
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, unitType={}, nameSeed={}, uniqueMods={}, champion={})", Name(),
                           Id(), Size(), unitId, unitType, nameSeed, ToHexString(uniqueMods), champion);
    }
};

static_assert(sizeof(D2GSPacketSrv57) == MonsterUniqueInfoPacket::SIZE);

}  // namespace d2bs::lod114d::packets
