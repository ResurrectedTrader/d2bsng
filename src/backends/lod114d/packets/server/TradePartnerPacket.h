#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class TradePartnerPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x78;
    static constexpr std::string_view NAME = "TradePartner";
    static constexpr uint32_t SIZE = 21;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::string name;
    uint32_t playerId = {};

    void Deserialize(BinaryReader& reader) override {
        name = reader.ReadString(16);
        playerId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](name={}, playerId={:#x})", Name(), Id(), Size(), name, playerId);
    }
};

static_assert(sizeof(D2GSPacketSrv78) == TradePartnerPacket::SIZE);

}  // namespace d2bs::lod114d::packets
