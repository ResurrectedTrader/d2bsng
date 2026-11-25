#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SpawnPlayerPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x7e;
    static constexpr std::string_view NAME = "SpawnPlayer";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::array<uint8_t, 4> unk0x01;

    void Deserialize(BinaryReader& reader) override { reader.ReadInto(unk0x01); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unk0x01={})", Name(), Id(), Size(), ToHexString(unk0x01));
    }
};

static_assert(sizeof(D2GSPacketSrv7E) == SpawnPlayerPacket::SIZE);

}  // namespace d2bs::lod114d::packets
