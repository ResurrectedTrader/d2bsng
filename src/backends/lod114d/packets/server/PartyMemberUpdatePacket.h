#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PartyMemberUpdatePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x7f;
    static constexpr std::string_view NAME = "PartyMemberUpdate";
    static constexpr uint32_t SIZE = 10;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    bool isPlayer = {};
    uint16_t lifePercentage = {};
    uint32_t unitId = {};
    game::LevelId levelId = {};

    void Deserialize(BinaryReader& reader) override {
        isPlayer = reader.ReadBoolean();
        lifePercentage = reader.ReadUInt16();
        unitId = reader.ReadUInt32();
        levelId = static_cast<game::LevelId>(reader.ReadUInt16());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](isPlayer={}, lifePercentage={}, unitId={:#x}, levelId={})", Name(), Id(),
                           Size(), isPlayer, lifePercentage, unitId, levelId);
    }
};

static_assert(sizeof(D2GSPacketSrv7F) == PartyMemberUpdatePacket::SIZE);

}  // namespace d2bs::lod114d::packets
