#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class CreateGamePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x67;
    static constexpr std::string_view NAME = "CreateGame";
    static constexpr uint32_t SIZE = 46;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::string gameName;
    uint8_t gameType = {};
    game::CharacterClass playerClass = {};
    int8_t characterTemplate = {};
    game::Difficulty difficulty = {};
    std::string clientName;
    int16_t arena = {};
    uint32_t gameFlags = {};
    int8_t unk0x2B = {};
    int8_t unk0x2C = {};
    int8_t locale = {};

    void Deserialize(BinaryReader& reader) override {
        gameName = reader.ReadString(16);
        gameType = reader.ReadByte();
        playerClass = static_cast<game::CharacterClass>(reader.ReadByte());
        characterTemplate = reader.Read<int8_t>();
        difficulty = static_cast<game::Difficulty>(reader.Read<int8_t>());
        clientName = reader.ReadString(16);
        arena = reader.ReadInt16();
        gameFlags = reader.ReadUInt32();
        unk0x2B = reader.Read<int8_t>();
        unk0x2C = reader.Read<int8_t>();
        locale = reader.Read<int8_t>();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](gameName={}, gameType={}, playerClass={}, characterTemplate={}, difficulty={}, "
            "clientName={}, arena={}, gameFlags={:#x}, unk0x2B={}, unk0x2C={}, locale={})",
            Name(), Id(), Size(), gameName, gameType, playerClass, characterTemplate, difficulty, clientName, arena,
            gameFlags, unk0x2B, unk0x2C, locale);
    }
};

static_assert(sizeof(D2GSPacketClt66) == CreateGamePacket::SIZE);

}  // namespace d2bs::lod114d::packets
