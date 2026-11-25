#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlayerKillCountPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x65;
    static constexpr std::string_view NAME = "PlayerKillCount";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    uint16_t killCount = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        killCount = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, killCount={})", Name(), Id(), Size(), unitId, killCount);
    }
};

static_assert(sizeof(D2GSPacketSrv65) == PlayerKillCountPacket::SIZE);

}  // namespace d2bs::lod114d::packets
