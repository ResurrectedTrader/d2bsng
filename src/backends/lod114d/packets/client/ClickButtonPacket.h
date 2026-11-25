#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A UI button - trade, stash gold, cube transmute (D2GAME_PACKETCALLBACK_Rcv0x4F_ClickButton). The parameter is two
// words or an item id, depending on the button.
class ClickButtonPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x4F;
    static constexpr std::string_view NAME = "ClickButton";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t buttonId = 0;
    uint32_t parameter = 0;

    void Deserialize(BinaryReader& reader) override {
        buttonId = reader.ReadUInt16();
        parameter = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](buttonId={}, parameter={:#x})", Name(), Id(), Size(), buttonId, parameter);
    }
};

static_assert(sizeof(D2GSPacketClt4F) == ClickButtonPacket::SIZE);

}  // namespace d2bs::lod114d::packets
