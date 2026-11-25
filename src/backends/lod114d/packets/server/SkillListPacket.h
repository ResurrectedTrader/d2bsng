#pragma once

#include <vector>

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Every skill a unit has and its level (SCMD_SendSkillList); 6 bytes plus 3 per skill.
class SkillListPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x94;
    static constexpr std::string_view NAME = "SkillList";

    struct Entry {
        game::Skill skill = {};
        uint8_t level = 0;
    };

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return 6 + (3 * static_cast<uint32_t>(skills.size())); }
    bool IsVariableSize() const override { return true; }

    uint32_t unitId = 0;
    std::vector<Entry> skills;

    void Deserialize(BinaryReader& reader) override {
        const uint8_t count = reader.ReadByte();
        unitId = reader.ReadUInt32();
        skills.clear();
        skills.reserve(count);
        for (uint8_t i = 0; i < count; ++i) {
            Entry entry;
            entry.skill = static_cast<game::Skill>(reader.ReadUInt16());
            entry.level = reader.ReadByte();
            skills.push_back(entry);
        }
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitId={:#x}, skills=[{}])", Name(), Id(), Size(), unitId,
            Join(skills, [](const Entry& entry) { return std::format("{}={}", entry.skill, entry.level); }));
    }
};

}  // namespace d2bs::lod114d::packets
