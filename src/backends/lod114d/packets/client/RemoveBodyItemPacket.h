#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class RemoveBodyItemPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x1c;
    static constexpr std::string_view NAME = "RemoveBodyItem";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::BodyLocation bodyLoc = {};

    void Deserialize(BinaryReader& reader) override { bodyLoc = static_cast<game::BodyLocation>(reader.ReadUInt16()); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](bodyLoc={})", Name(), Id(), Size(), bodyLoc);
    }
};

static_assert(sizeof(D2GSPacketClt1C) == RemoveBodyItemPacket::SIZE);

}  // namespace d2bs::lod114d::packets
