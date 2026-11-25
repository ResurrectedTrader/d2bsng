#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class UnitOverlayPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x11;
    static constexpr std::string_view NAME = "UnitOverlay";
    static constexpr uint32_t SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint16_t overlay = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        overlay = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, overlay={})", Name(), Id(), Size(), unitType,
                           unitId, overlay);
    }
};

static_assert(sizeof(D2GSPacketSrv11) == UnitOverlayPacket::SIZE);

}  // namespace d2bs::lod114d::packets
