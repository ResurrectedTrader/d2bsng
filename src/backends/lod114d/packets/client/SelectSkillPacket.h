#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SelectSkillPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3c;
    static constexpr std::string_view NAME = "SelectSkill";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Skill skill = {};
    uint16_t mode = {};
    uint32_t flags = {};

    void Deserialize(BinaryReader& reader) override {
        skill = static_cast<game::Skill>(reader.ReadUInt16());
        mode = reader.ReadUInt16();
        flags = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](skill={}, mode={:#x}, flags={:#x})", Name(), Id(), Size(), skill, mode,
                           flags);
    }
};

static_assert(sizeof(D2GSPacketClt3C) == SelectSkillPacket::SIZE);

}  // namespace d2bs::lod114d::packets
