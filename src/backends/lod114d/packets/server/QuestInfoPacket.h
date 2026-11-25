#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestInfoPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x28;
    static constexpr std::string_view NAME = "QuestInfo";
    static constexpr uint32_t SIZE = 103;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t terminateType = {};
    std::array<uint8_t, 96> data;

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        terminateType = reader.ReadByte();
        reader.ReadInto(data);
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, terminateType={}, data={})", Name(), Id(), Size(),
                           unitType, unitId, terminateType, ToHexString(data));
    }
};

static_assert(sizeof(D2GSPacketSrv28) == QuestInfoPacket::SIZE);

}  // namespace d2bs::lod114d::packets
