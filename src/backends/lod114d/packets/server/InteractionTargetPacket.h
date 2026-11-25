#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class InteractionTargetPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x62;
    static constexpr std::string_view NAME = "InteractionTarget";
    static constexpr uint32_t SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t unk0x06 = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        unk0x06 = reader.ReadByte();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, unk0x06={})", Name(), Id(), Size(), unitType,
                           unitId, unk0x06);
    }
};

static_assert(sizeof(D2GSPacketSrv62) == InteractionTargetPacket::SIZE);

}  // namespace d2bs::lod114d::packets
