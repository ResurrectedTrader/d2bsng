#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class BindHotkeyToSkillPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x51;
    static constexpr std::string_view NAME = "BindHotkeyToSkill";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Skill skillId = {};
    uint32_t ownerId = {};

    void Deserialize(BinaryReader& reader) override {
        skillId = static_cast<game::Skill>(reader.ReadInt32());
        ownerId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](skillId={}, ownerId={:#x})", Name(), Id(), Size(), skillId, ownerId);
    }
};

static_assert(sizeof(D2GSPacketClt51) == BindHotkeyToSkillPacket::SIZE);

}  // namespace d2bs::lod114d::packets
