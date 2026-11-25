#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignPartyPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x8d;
    static constexpr std::string_view NAME = "AssignParty";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t playerId = {};
    uint16_t partyId = {};

    void Deserialize(BinaryReader& reader) override {
        playerId = reader.ReadUInt32();
        partyId = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](playerId={:#x}, partyId={})", Name(), Id(), Size(), playerId, partyId);
    }
};

static_assert(sizeof(D2GSPacketSrv8D) == AssignPartyPacket::SIZE);

}  // namespace d2bs::lod114d::packets
