#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class RefreshInventoryPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x48;
    static constexpr std::string_view NAME = "RefreshInventory";
    static constexpr uint32_t SIZE = 11;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint8_t unk0x02 = {};
    uint32_t unitId = {};
    uint32_t unk0x07 = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unk0x02 = reader.ReadByte();
        unitId = reader.ReadUInt32();
        unk0x07 = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unk0x02={}, unitId={:#x}, unk0x07={})", Name(), Id(), Size(),
                           unitType, unk0x02, unitId, unk0x07);
    }
};

static_assert(sizeof(D2GSPacketSrv48) == RefreshInventoryPacket::SIZE);

}  // namespace d2bs::lod114d::packets
