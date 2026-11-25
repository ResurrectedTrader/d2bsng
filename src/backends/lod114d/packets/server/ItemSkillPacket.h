#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ItemSkillPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3f;
    static constexpr std::string_view NAME = "ItemSkill";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t cursor = {};
    uint32_t itemId = {};
    game::Skill skill = {};

    void Deserialize(BinaryReader& reader) override {
        cursor = reader.ReadByte();
        itemId = reader.ReadUInt32();
        skill = static_cast<game::Skill>(reader.ReadUInt16());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](cursor={}, itemId={:#x}, skill={})", Name(), Id(), Size(), cursor, itemId,
                           skill);
    }
};

static_assert(sizeof(D2GSPacketSrv3F) == ItemSkillPacket::SIZE);

}  // namespace d2bs::lod114d::packets
