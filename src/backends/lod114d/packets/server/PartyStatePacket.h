#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PartyStatePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x8b;
    static constexpr std::string_view NAME = "PartyState";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    bool party = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        party = reader.ReadBoolean();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, party={})", Name(), Id(), Size(), unitId, party);
    }
};

static_assert(sizeof(D2GSPacketSrv8B) == PartyStatePacket::SIZE);

}  // namespace d2bs::lod114d::packets
