#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AddGoldPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x19;
    static constexpr std::string_view NAME = "AddGold";
    static constexpr uint32_t SIZE = 2;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t gold = {};

    void Deserialize(BinaryReader& reader) override { gold = reader.ReadByte(); }

    std::string ToString() const override { return std::format("{}[{:#x}, {}](gold={})", Name(), Id(), Size(), gold); }
};

static_assert(sizeof(D2GSPacketSrv19) == AddGoldPacket::SIZE);

}  // namespace d2bs::lod114d::packets
