#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SwapCursorItemWithBodyPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x1d;
    static constexpr std::string_view NAME = "SwapCursorItemWithBody";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t itemId = {};
    game::BodyLocation bodyLoc = {};

    void Deserialize(BinaryReader& reader) override {
        itemId = reader.ReadUInt32();
        bodyLoc = static_cast<game::BodyLocation>(reader.ReadInt32());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](itemId={:#x}, bodyLoc={})", Name(), Id(), Size(), itemId, bodyLoc);
    }
};

static_assert(sizeof(D2GSPacketClt1D) == SwapCursorItemWithBodyPacket::SIZE);

}  // namespace d2bs::lod114d::packets
