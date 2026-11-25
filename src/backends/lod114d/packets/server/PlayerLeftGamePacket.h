#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlayerLeftGamePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5c;
    static constexpr std::string_view NAME = "PlayerLeftGame";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t playerId = {};

    void Deserialize(BinaryReader& reader) override { playerId = reader.ReadUInt32(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](playerId={:#x})", Name(), Id(), Size(), playerId);
    }
};

static_assert(sizeof(D2GSPacketSrv5C) == PlayerLeftGamePacket::SIZE);

}  // namespace d2bs::lod114d::packets
