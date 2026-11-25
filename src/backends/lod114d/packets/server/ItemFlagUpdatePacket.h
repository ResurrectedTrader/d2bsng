#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ItemFlagUpdatePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x7d;
    static constexpr std::string_view NAME = "ItemFlagUpdate";
    static constexpr uint32_t SIZE = 18;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId1 = {};
    uint32_t unitId2 = {};
    game::ItemFlag flag = {};
    bool set = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId1 = reader.ReadUInt32();
        unitId2 = reader.ReadUInt32();
        flag = static_cast<game::ItemFlag>(reader.ReadUInt32());
        set = reader.ReadUInt32() != 0;
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId1={}, unitId2={}, flag={}, set={})", Name(), Id(), Size(),
                           unitType, unitId1, unitId2, flag, set);
    }
};

static_assert(sizeof(D2GSPacketSrv7D) == ItemFlagUpdatePacket::SIZE);

}  // namespace d2bs::lod114d::packets
