#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class InteractionResultPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x58;
    static constexpr std::string_view NAME = "InteractionResult";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint8_t action = {};
    bool success = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        action = reader.ReadByte();
        success = reader.ReadBoolean();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, action={}, success={})", Name(), Id(), Size(), unitId, action,
                           success);
    }
};

static_assert(sizeof(D2GSPacketSrv58) == InteractionResultPacket::SIZE);

}  // namespace d2bs::lod114d::packets
