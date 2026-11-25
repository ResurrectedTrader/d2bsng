#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A skill bound to a hotkey slot (D2GAME_PACKETS_SendPacket0x7B): the skill and hand share one word, 12 + 4 bits.
class SkillHotkeyPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x7B;
    static constexpr std::string_view NAME = "SkillHotkey";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t slot = 0;
    game::Skill skill = {};
    uint8_t hand = 0;
    uint32_t itemId = 0;

    void Deserialize(BinaryReader& reader) override {
        slot = reader.ReadByte();
        const uint16_t packed = reader.ReadUInt16();
        skill = static_cast<game::Skill>(packed & 0x0FFFU);
        hand = static_cast<uint8_t>(packed >> 12U);
        itemId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](slot={}, skill={}, hand={}, itemId={:#x})", Name(), Id(), Size(), slot, skill,
                           hand, itemId);
    }
};

static_assert(sizeof(D2GSPacketSrv7B) == SkillHotkeyPacket::SIZE);

}  // namespace d2bs::lod114d::packets
