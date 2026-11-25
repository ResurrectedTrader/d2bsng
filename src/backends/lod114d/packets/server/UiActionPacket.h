#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UiActionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x77;
    static constexpr std::string_view NAME = "UiAction";
    static constexpr uint32_t SIZE = 2;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    UiAction action = {};

    void Deserialize(BinaryReader& reader) override { action = static_cast<UiAction>(reader.ReadByte()); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](action={})", Name(), Id(), Size(), action);
    }
};

static_assert(sizeof(D2GSPacketSrv77) == UiActionPacket::SIZE);

}  // namespace d2bs::lod114d::packets
