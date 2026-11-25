#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SkillQuantityPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x22;
    static constexpr std::string_view NAME = "SkillQuantity";
    static constexpr uint32_t SIZE = 12;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint8_t unk0x02 = {};
    uint32_t unitId = {};
    game::Skill skillId = {};
    uint8_t quantity = {};
    uint8_t unk0x0A = {};
    bool isCorpse = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unk0x02 = reader.ReadByte();
        unitId = reader.ReadUInt32();
        skillId = static_cast<game::Skill>(reader.ReadUInt16());
        quantity = reader.ReadByte();
        unk0x0A = reader.ReadByte();
        isCorpse = reader.ReadBoolean();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitType={}, unk0x02={}, unitId={:#x}, skillId={}, quantity={}, unk0x0A={}, isCorpse={})",
            Name(), Id(), Size(), unitType, unk0x02, unitId, skillId, quantity, unk0x0A, isCorpse);
    }
};

static_assert(sizeof(D2GSPacketSrv22) == SkillQuantityPacket::SIZE);

}  // namespace d2bs::lod114d::packets
