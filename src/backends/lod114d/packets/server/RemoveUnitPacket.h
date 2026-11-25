#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class RemoveUnitPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x0a;
    static constexpr std::string_view NAME = "RemoveUnit";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x})", Name(), Id(), Size(), unitType, unitId);
    }
};

static_assert(sizeof(D2GSPacketSrv0A) == RemoveUnitPacket::SIZE);

}  // namespace d2bs::lod114d::packets
