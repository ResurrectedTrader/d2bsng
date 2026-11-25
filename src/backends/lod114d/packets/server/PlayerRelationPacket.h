#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlayerRelationPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x8c;
    static constexpr std::string_view NAME = "PlayerRelation";
    static constexpr uint32_t SIZE = 11;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::array<uint8_t, 8> playerId;
    uint16_t flags = {};

    void Deserialize(BinaryReader& reader) override {
        reader.ReadInto(playerId);
        flags = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](playerId={}, flags={:#x})", Name(), Id(), Size(), ToHexString(playerId),
                           flags);
    }
};

static_assert(sizeof(D2GSPacketSrv8C) == PlayerRelationPacket::SIZE);

}  // namespace d2bs::lod114d::packets
