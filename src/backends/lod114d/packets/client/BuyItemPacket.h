#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Buys an item from an NPC's store (D2GAME_PACKETCALLBACK_Rcv0x32_BuyItemFromNpcBuffer). The word after the
// transaction type packs the item mode (15 bits) and a buy-a-stack bit.
class BuyItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x32;
    static constexpr std::string_view NAME = "BuyItem";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t npcId = 0;
    uint32_t itemId = 0;
    uint16_t transactionType = 0;
    uint16_t itemMode = 0;
    bool isMultibuy = false;
    uint32_t cost = 0;

    void Deserialize(BinaryReader& reader) override {
        npcId = reader.ReadUInt32();
        itemId = reader.ReadUInt32();
        transactionType = reader.ReadUInt16();
        const uint16_t packed = reader.ReadUInt16();
        itemMode = packed & 0x7FFFU;
        isMultibuy = (packed & 0x8000U) != 0;
        cost = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](npcId={:#x}, itemId={:#x}, transactionType={}, itemMode={}, multibuy={}, "
                           "cost={})",
                           Name(), Id(), Size(), npcId, itemId, transactionType, itemMode, isMultibuy, cost);
    }
};

static_assert(sizeof(D2GSPacketClt32) == BuyItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
