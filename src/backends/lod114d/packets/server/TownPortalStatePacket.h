#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class TownPortalStatePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x60;
    static constexpr std::string_view NAME = "TownPortalState";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t flags = {};
    uint8_t level = {};
    uint32_t unitId = {};

    void Deserialize(BinaryReader& reader) override {
        flags = reader.ReadByte();
        level = reader.ReadByte();
        unitId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](flags={}, level={}, unitId={:#x})", Name(), Id(), Size(), flags, level,
                           unitId);
    }
};

static_assert(sizeof(D2GSPacketSrv60) == TownPortalStatePacket::SIZE);

}  // namespace d2bs::lod114d::packets
