#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ItemToBeltPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x23;
    static constexpr std::string_view NAME = "ItemToBelt";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t cursorItemId = {};
    int32_t beltSlot = {};

    void Deserialize(BinaryReader& reader) override {
        cursorItemId = reader.ReadUInt32();
        beltSlot = reader.ReadInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](cursorItemId={:#x}, beltSlot={})", Name(), Id(), Size(), cursorItemId,
                           beltSlot);
    }
};

static_assert(sizeof(D2GSPacketClt23) == ItemToBeltPacket::SIZE);

}  // namespace d2bs::lod114d::packets
