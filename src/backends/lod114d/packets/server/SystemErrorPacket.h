#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class SystemErrorPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xb4;
    static constexpr std::string_view NAME = "SystemError";
    static constexpr uint32_t SIZE = 5;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t errorCode = {};

    void Deserialize(BinaryReader& reader) override { errorCode = reader.ReadUInt32(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](errorCode={})", Name(), Id(), Size(), errorCode);
    }
};

static_assert(sizeof(D2GSPacketSrvB3) == SystemErrorPacket::SIZE);

}  // namespace d2bs::lod114d::packets
