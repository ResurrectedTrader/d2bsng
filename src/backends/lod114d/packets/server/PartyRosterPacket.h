#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PartyRosterPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x75;
    static constexpr std::string_view NAME = "PartyRoster";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint16_t partyId = {};
    uint16_t level = {};
    uint16_t flags = {};
    uint16_t partyFlags = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        partyId = reader.ReadUInt16();
        level = reader.ReadUInt16();
        flags = reader.ReadUInt16();
        partyFlags = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, partyId={}, level={}, flags={:#x}, partyFlags={:#x})", Name(),
                           Id(), Size(), unitId, partyId, level, flags, partyFlags);
    }
};

static_assert(sizeof(D2GSPacketSrv75) == PartyRosterPacket::SIZE);

}  // namespace d2bs::lod114d::packets
