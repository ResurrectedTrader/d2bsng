#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MercenaryReviveCostPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x9b;
    static constexpr std::string_view NAME = "MercenaryReviveCost";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t mercNameStringId = {};
    uint32_t reviveCost = {};

    void Deserialize(BinaryReader& reader) override {
        mercNameStringId = reader.ReadUInt16();
        reviveCost = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](mercNameStringId={}, reviveCost={})", Name(), Id(), Size(), mercNameStringId,
                           reviveCost);
    }
};

static_assert(sizeof(D2GSPacketSrv9B) == MercenaryReviveCostPacket::SIZE);

}  // namespace d2bs::lod114d::packets
