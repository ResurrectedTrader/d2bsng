#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class DayNightCyclePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x53;
    static constexpr std::string_view NAME = "DayNightCycle";
    static constexpr uint32_t SIZE = 10;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    int32_t index = {};
    int32_t ticks = {};
    bool eclipse = {};

    void Deserialize(BinaryReader& reader) override {
        index = reader.ReadInt32();
        ticks = reader.ReadInt32();
        eclipse = reader.ReadBoolean();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](index={}, ticks={}, eclipse={})", Name(), Id(), Size(), index, ticks,
                           eclipse);
    }
};

static_assert(sizeof(D2GSPacketSrv53) == DayNightCyclePacket::SIZE);

}  // namespace d2bs::lod114d::packets
