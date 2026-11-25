#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Spends stat points (D2GAME_PACKETCALLBACK_Rcv0x3A_AddStatPoint): the low byte is the stat, the high byte the point
// count less one.
class AddStatPointPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3A;
    static constexpr std::string_view NAME = "AddStatPoint";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Stat stat = {};
    uint32_t points = 0;

    void Deserialize(BinaryReader& reader) override {
        stat = static_cast<game::Stat>(reader.ReadByte());
        points = reader.ReadByte() + 1U;
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](stat={}, points={})", Name(), Id(), Size(), stat, points);
    }
};

static_assert(sizeof(D2GSPacketClt3A) == AddStatPointPacket::SIZE);

}  // namespace d2bs::lod114d::packets
