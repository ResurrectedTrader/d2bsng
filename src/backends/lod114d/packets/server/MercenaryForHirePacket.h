#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MercenaryForHirePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x4e;
    static constexpr std::string_view NAME = "MercenaryForHire";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t mercName = {};
    uint32_t seed = {};

    void Deserialize(BinaryReader& reader) override {
        mercName = reader.ReadUInt16();
        seed = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](mercName={}, seed={:#x})", Name(), Id(), Size(), mercName, seed);
    }
};

static_assert(sizeof(D2GSPacketSrv4E) == MercenaryForHirePacket::SIZE);

}  // namespace d2bs::lod114d::packets
