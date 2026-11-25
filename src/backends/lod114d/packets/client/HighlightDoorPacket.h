#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class HighlightDoorPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3d;
    static constexpr std::string_view NAME = "HighlightDoor";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t doorId = {};

    void Deserialize(BinaryReader& reader) override { doorId = reader.ReadUInt32(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](doorId={:#x})", Name(), Id(), Size(), doorId);
    }
};

static_assert(sizeof(D2GSPacketClt3D) == HighlightDoorPacket::SIZE);

}  // namespace d2bs::lod114d::packets
