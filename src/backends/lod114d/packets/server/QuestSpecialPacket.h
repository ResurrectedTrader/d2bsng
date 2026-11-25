#pragma once

#include <array>

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A quest's extra state - Cain's stone order, Tal Rasha's tomb, the hireling a quest gives (D2GAME_SendPacket0x50).
// The payload's layout depends on the quest, so it prints as bytes.
class QuestSpecialPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x50;
    static constexpr std::string_view NAME = "QuestSpecial";
    static constexpr uint32_t SIZE = 15;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint16_t questId = 0;
    std::array<uint8_t, SIZE - 3> payload = {};

    void Deserialize(BinaryReader& reader) override {
        questId = reader.ReadUInt16();
        reader.ReadInto(payload);
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](questId={}, payload=[{}])", Name(), Id(), Size(), questId,
                           ToHexString(payload));
    }
};

static_assert(sizeof(D2GSPacketSrv50) == QuestSpecialPacket::SIZE);

}  // namespace d2bs::lod114d::packets
