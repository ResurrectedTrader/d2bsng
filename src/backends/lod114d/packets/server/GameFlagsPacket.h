#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class GameFlagsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x01;
    static constexpr std::string_view NAME = "GameFlags";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Difficulty difficulty = {};
    uint32_t arenaFlags = {};
    uint8_t expansion = {};
    uint8_t ladder = {};

    void Deserialize(BinaryReader& reader) override {
        difficulty = static_cast<game::Difficulty>(reader.ReadByte());
        arenaFlags = reader.ReadUInt32();
        expansion = reader.ReadByte();
        ladder = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](difficulty={}, arenaFlags={:#x}, expansion={}, ladder={})", Name(), Id(),
                           Size(), difficulty, arenaFlags, expansion, ladder);
    }
};

static_assert(sizeof(D2GSPacketSrv01) == GameFlagsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
