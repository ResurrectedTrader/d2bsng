#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AddSkillPointPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3b;
    static constexpr std::string_view NAME = "AddSkillPoint";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Skill skill = {};

    void Deserialize(BinaryReader& reader) override { skill = static_cast<game::Skill>(reader.ReadUInt16()); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](skill={})", Name(), Id(), Size(), skill);
    }
};

static_assert(sizeof(D2GSPacketClt3B) == AddSkillPointPacket::SIZE);

}  // namespace d2bs::lod114d::packets
