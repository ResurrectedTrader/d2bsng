#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PartyActionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5e;
    static constexpr std::string_view NAME = "PartyAction";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t type = {};
    uint32_t playerId = {};

    void Deserialize(BinaryReader& reader) override {
        type = reader.ReadByte();
        playerId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](type={}, playerId={:#x})", Name(), Id(), Size(), type, playerId);
    }
};

static_assert(sizeof(D2GSPacketClt5E) == PartyActionPacket::SIZE);

}  // namespace d2bs::lod114d::packets
