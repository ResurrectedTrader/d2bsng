#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlaySoundPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x2c;
    static constexpr std::string_view NAME = "PlaySound";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint16_t soundEvent = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        soundEvent = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, soundEvent={})", Name(), Id(), Size(), unitType,
                           unitId, soundEvent);
    }
};

static_assert(sizeof(D2GSPacketSrv2C) == PlaySoundPacket::SIZE);

}  // namespace d2bs::lod114d::packets
