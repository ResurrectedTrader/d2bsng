#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UseItemOnItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x27;
    static constexpr std::string_view NAME = "UseItemOnItem";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t targetItemId = {};
    uint32_t useItemId = {};

    void Deserialize(BinaryReader& reader) override {
        targetItemId = reader.ReadUInt32();
        useItemId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](targetItemId={:#x}, useItemId={:#x})", Name(), Id(), Size(), targetItemId,
                           useItemId);
    }
};

static_assert(sizeof(D2GSPacketClt27) == UseItemOnItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
