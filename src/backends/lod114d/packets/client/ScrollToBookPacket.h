#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ScrollToBookPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x29;
    static constexpr std::string_view NAME = "ScrollToBook";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t scrollId = {};
    uint32_t bookId = {};

    void Deserialize(BinaryReader& reader) override {
        scrollId = reader.ReadUInt32();
        bookId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](scrollId={:#x}, bookId={:#x})", Name(), Id(), Size(), scrollId, bookId);
    }
};

static_assert(sizeof(D2GSPacketClt29) == ScrollToBookPacket::SIZE);

}  // namespace d2bs::lod114d::packets
