#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestIntroFlagsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5e;
    static constexpr std::string_view NAME = "QuestIntroFlags";
    static constexpr uint32_t SIZE = 38;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    std::array<uint8_t, 37> questIntroFlags;

    void Deserialize(BinaryReader& reader) override { reader.ReadInto(questIntroFlags); }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](questIntroFlags={})", Name(), Id(), Size(), ToHexString(questIntroFlags));
    }
};

static_assert(sizeof(D2GSPacketSrv5E) == QuestIntroFlagsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
