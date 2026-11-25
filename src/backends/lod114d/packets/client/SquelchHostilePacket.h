#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SquelchHostilePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5d;
    static constexpr std::string_view NAME = "SquelchHostile";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t type = {};
    uint8_t toggle = {};
    uint32_t playerId = {};

    void Deserialize(BinaryReader& reader) override {
        type = reader.ReadByte();
        toggle = reader.ReadByte();
        playerId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](type={}, toggle={}, playerId={:#x})", Name(), Id(), Size(), type, toggle,
                           playerId);
    }
};

static_assert(sizeof(D2GSPacketClt5D) == SquelchHostilePacket::SIZE);

}  // namespace d2bs::lod114d::packets
