#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class TradeGoldPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x79;
    static constexpr std::string_view NAME = "TradeGold";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    bool ownGold = {};
    uint32_t gold = {};

    void Deserialize(BinaryReader& reader) override {
        ownGold = reader.ReadBoolean();
        gold = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](ownGold={}, gold={})", Name(), Id(), Size(), ownGold, gold);
    }
};

static_assert(sizeof(D2GSPacketSrv79) == TradeGoldPacket::SIZE);

}  // namespace d2bs::lod114d::packets
