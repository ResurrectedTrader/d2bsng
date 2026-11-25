#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PortalOwnerPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x82;
    static constexpr std::string_view NAME = "PortalOwner";
    static constexpr uint32_t SIZE = 29;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t ownerId = {};
    std::string ownerName;
    uint32_t portalId = {};
    uint32_t destPortalId = {};

    void Deserialize(BinaryReader& reader) override {
        ownerId = reader.ReadUInt32();
        ownerName = reader.ReadString(16);
        portalId = reader.ReadUInt32();
        destPortalId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](ownerId={:#x}, ownerName={}, portalId={:#x}, destPortalId={:#x})", Name(),
                           Id(), Size(), ownerId, ownerName, portalId, destPortalId);
    }
};

static_assert(sizeof(D2GSPacketSrv82) == PortalOwnerPacket::SIZE);

}  // namespace d2bs::lod114d::packets
