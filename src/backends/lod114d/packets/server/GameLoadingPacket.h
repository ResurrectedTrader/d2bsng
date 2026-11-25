#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class GameLoadingPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x00;
    static constexpr std::string_view NAME = "GameLoading";
    static constexpr uint32_t SIZE = 1;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    void Deserialize(BinaryReader& /*reader*/) override {}

    std::string ToString() const override { return std::format("{}[{:#x}, {}]", Name(), Id(), Size()); }
};

static_assert(sizeof(D2GSPacketSrv00) == GameLoadingPacket::SIZE);

}  // namespace d2bs::lod114d::packets
