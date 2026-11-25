#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PortalFlagsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5f;
    static constexpr std::string_view NAME = "PortalFlags";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t portalFlags = {};

    void Deserialize(BinaryReader& reader) override { portalFlags = reader.ReadUInt32(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](portalFlags={:#x})", Name(), Id(), Size(), portalFlags);
    }
};

static_assert(sizeof(D2GSPacketSrv5F) == PortalFlagsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
