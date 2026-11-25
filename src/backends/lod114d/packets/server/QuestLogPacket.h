#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestLogPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x52;
    static constexpr std::string_view NAME = "QuestLog";
    static constexpr uint32_t SIZE = 42;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::array<uint8_t, 41> questList;

    void Deserialize(BinaryReader& reader) override { reader.ReadInto(questList); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](questList={})", Name(), Id(), Size(), ToHexString(questList));
    }
};

static_assert(sizeof(D2GSPacketSrv52) == QuestLogPacket::SIZE);

}  // namespace d2bs::lod114d::packets
