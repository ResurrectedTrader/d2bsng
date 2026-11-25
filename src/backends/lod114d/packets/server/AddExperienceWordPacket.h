#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AddExperienceWordPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x1b;
    static constexpr std::string_view NAME = "AddExperienceWord";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t experience = {};

    void Deserialize(BinaryReader& reader) override { experience = reader.ReadUInt16(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](experience={})", Name(), Id(), Size(), experience);
    }
};

static_assert(sizeof(D2GSPacketSrv1B) == AddExperienceWordPacket::SIZE);

}  // namespace d2bs::lod114d::packets
