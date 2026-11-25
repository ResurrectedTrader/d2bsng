#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class MercInteractPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x46;
    static constexpr std::string_view NAME = "MercInteract";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t mercId = {};
    uint32_t unitId = {};
    game::UnitType unitType = {};

    void Deserialize(BinaryReader& reader) override {
        mercId = reader.ReadUInt32();
        unitId = reader.ReadUInt32();
        unitType = static_cast<game::UnitType>(reader.ReadInt32());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](mercId={:#x}, unitId={:#x}, unitType={})", Name(), Id(), Size(), mercId,
                           unitId, unitType);
    }
};

static_assert(sizeof(D2GSPacketClt46) == MercInteractPacket::SIZE);

}  // namespace d2bs::lod114d::packets
