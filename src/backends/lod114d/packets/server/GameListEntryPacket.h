#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class GameListEntryPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xb2;
    static constexpr std::string_view NAME = "GameListEntry";
    static constexpr uint32_t SIZE = 53;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::string gameName;
    uint16_t clientCount = {};
    uint16_t gameId = {};

    void Deserialize(BinaryReader& reader) override {
        gameName = reader.ReadString(48);
        clientCount = reader.ReadUInt16();
        gameId = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](gameName={}, clientCount={}, gameId={})", Name(), Id(), Size(), gameName,
                           clientCount, gameId);
    }
};

static_assert(sizeof(D2GSPacketSrvB1) == GameListEntryPacket::SIZE);

}  // namespace d2bs::lod114d::packets
