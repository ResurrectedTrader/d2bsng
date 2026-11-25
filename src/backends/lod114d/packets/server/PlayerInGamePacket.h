#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A player joins the game's roster (SCMD_SendPlayerJoinAndPets). Its own length field sizes it.
class PlayerInGamePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5B;
    static constexpr std::string_view NAME = "PlayerInGame";
    static constexpr uint32_t NAME_SIZE = 16;
    // Everything up to the optional trailing data.
    static constexpr uint32_t FIXED_SIZE = 34;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetLength; }
    bool IsVariableSize() const override { return true; }

    uint16_t packetLength = 0;
    uint32_t unitId = 0;
    game::CharacterClass characterClass = {};
    std::string name;
    uint16_t level = 0;
    uint16_t partyId = 0;
    uint16_t unk0x1C = 0;
    uint16_t partyFlags = 0;
    uint16_t guildFlags = 0;
    std::string trailer;

    void Deserialize(BinaryReader& reader) override {
        packetLength = reader.ReadUInt16();
        unitId = reader.ReadUInt32();
        characterClass = static_cast<game::CharacterClass>(reader.ReadByte());
        name = reader.ReadString(NAME_SIZE);
        level = reader.ReadUInt16();
        partyId = reader.ReadUInt16();
        unk0x1C = reader.ReadUInt16();
        partyFlags = reader.ReadUInt16();
        guildFlags = reader.ReadUInt16();
        if (packetLength < FIXED_SIZE) {
            throw IoException("player packet shorter than its head");
        }
        trailer = ToHexString(reader.ReadSpan(packetLength - FIXED_SIZE));
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](unitId={:#x}, class={}, name={}, level={}, partyId={}, unk0x1C={}, partyFlags={:#x}, "
            "guildFlags={:#x}, trailer=[{}])",
            Name(), Id(), Size(), unitId, characterClass, name, level, partyId, unk0x1C, partyFlags, guildFlags,
            trailer);
    }
};

}  // namespace d2bs::lod114d::packets
