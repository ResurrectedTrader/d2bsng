#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SocketItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x28;
    static constexpr std::string_view NAME = "SocketItem";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t cursorItemId = {};
    uint32_t targetItemId = {};

    void Deserialize(BinaryReader& reader) override {
        cursorItemId = reader.ReadUInt32();
        targetItemId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](cursorItemId={:#x}, targetItemId={:#x})", Name(), Id(), Size(), cursorItemId,
                           targetItemId);
    }
};

static_assert(sizeof(D2GSPacketClt28) == SocketItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
