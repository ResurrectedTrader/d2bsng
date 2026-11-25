#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Joins a game on a game server (D2CLTSYS_JOINGAME). 1.14d adds two constants after the version, so the client name
// starts at 0x15 rather than 0x0D.
class JoinGamePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x68;
    static constexpr std::string_view NAME = "JoinGame";
    static constexpr uint32_t SIZE = 37;
    static constexpr uint32_t NAME_SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t tokenId = 0;
    uint16_t gameId = 0;
    game::CharacterClass playerClass = {};
    uint32_t version = 0;
    uint32_t productConstant = 0;
    uint32_t joinConstant = 0;
    uint8_t locale = 0;
    std::string clientName;

    void Deserialize(BinaryReader& reader) override {
        tokenId = reader.ReadUInt32();
        gameId = reader.ReadUInt16();
        playerClass = static_cast<game::CharacterClass>(reader.ReadByte());
        version = reader.ReadUInt32();
        productConstant = reader.ReadUInt32();
        joinConstant = reader.ReadUInt32();
        locale = reader.ReadByte();
        clientName = reader.ReadString(NAME_SIZE);
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](tokenId={:#x}, gameId={}, playerClass={}, version={:#x}, productConstant={:#x}, "
            "joinConstant={:#x}, locale={}, clientName={})",
            Name(), Id(), Size(), tokenId, gameId, playerClass, version, productConstant, joinConstant, locale,
            clientName);
    }
};

static_assert(sizeof(D2GSPacketClt67) == JoinGamePacket::SIZE);

}  // namespace d2bs::lod114d::packets
