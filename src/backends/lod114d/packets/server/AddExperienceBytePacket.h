#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AddExperienceBytePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x1a;
    static constexpr std::string_view NAME = "AddExperienceByte";
    static constexpr uint32_t SIZE = 2;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t experience = {};

    void Deserialize(BinaryReader& reader) override { experience = reader.ReadByte(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](experience={})", Name(), Id(), Size(), experience);
    }
};

static_assert(sizeof(D2GSPacketSrv1A) == AddExperienceBytePacket::SIZE);

}  // namespace d2bs::lod114d::packets
