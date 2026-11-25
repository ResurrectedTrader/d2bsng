#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class CinematicPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x61;
    static constexpr std::string_view NAME = "Cinematic";
    static constexpr uint32_t SIZE = 2;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t cinematic = {};

    void Deserialize(BinaryReader& reader) override { cinematic = reader.ReadByte(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](cinematic={})", Name(), Id(), Size(), cinematic);
    }
};

static_assert(sizeof(D2GSPacketSrv61) == CinematicPacket::SIZE);

}  // namespace d2bs::lod114d::packets
