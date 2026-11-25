#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class HireMercPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x36;
    static constexpr std::string_view NAME = "HireMerc";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t npcId = {};
    uint32_t hirelingId = {};

    void Deserialize(BinaryReader& reader) override {
        npcId = reader.ReadUInt32();
        hirelingId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](npcId={:#x}, hirelingId={:#x})", Name(), Id(), Size(), npcId, hirelingId);
    }
};

static_assert(sizeof(D2GSPacketClt36) == HireMercPacket::SIZE);

}  // namespace d2bs::lod114d::packets
