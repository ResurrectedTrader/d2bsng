#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class QuestStatePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5d;
    static constexpr std::string_view NAME = "QuestState";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t questId = {};
    uint8_t flags = {};
    game::State state = {};
    uint16_t unk0x04 = {};

    void Deserialize(BinaryReader& reader) override {
        questId = reader.ReadByte();
        flags = reader.ReadByte();
        state = static_cast<game::State>(reader.ReadByte());
        unk0x04 = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](questId={}, flags={}, state={}, unk0x04={})", Name(), Id(), Size(), questId,
                           flags, state, unk0x04);
    }
};

static_assert(sizeof(D2GSPacketSrv5D) == QuestStatePacket::SIZE);

}  // namespace d2bs::lod114d::packets
