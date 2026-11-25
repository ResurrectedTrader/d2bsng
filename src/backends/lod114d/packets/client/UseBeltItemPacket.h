#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UseBeltItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x26;
    static constexpr std::string_view NAME = "UseBeltItem";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t itemId = {};
    bool onMercenary = {};
    int32_t unk0x09 = {};

    void Deserialize(BinaryReader& reader) override {
        itemId = reader.ReadUInt32();
        onMercenary = reader.ReadUInt32() != 0;
        unk0x09 = reader.ReadInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](itemId={:#x}, onMercenary={}, unk0x09={})", Name(), Id(), Size(), itemId,
                           onMercenary, unk0x09);
    }
};

static_assert(sizeof(D2GSPacketClt26) == UseBeltItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
