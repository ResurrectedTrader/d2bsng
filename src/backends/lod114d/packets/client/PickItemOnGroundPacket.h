#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PickItemOnGroundPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x16;
    static constexpr std::string_view NAME = "PickItemOnGround";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    bool toCursor = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadUInt32());
        unitId = reader.ReadUInt32();
        toCursor = reader.ReadUInt32() != 0;
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, toCursor={})", Name(), Id(), Size(), unitType,
                           unitId, toCursor);
    }
};

static_assert(sizeof(D2GSPacketClt16) == PickItemOnGroundPacket::SIZE);

}  // namespace d2bs::lod114d::packets
