#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlayAudioPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3f;
    static constexpr std::string_view NAME = "PlayAudio";
    static constexpr uint32_t SIZE = 3;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t soundId = {};

    void Deserialize(BinaryReader& reader) override { soundId = reader.ReadUInt16(); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](soundId={})", Name(), Id(), Size(), soundId);
    }
};

static_assert(sizeof(D2GSPacketClt3F) == PlayAudioPacket::SIZE);

}  // namespace d2bs::lod114d::packets
