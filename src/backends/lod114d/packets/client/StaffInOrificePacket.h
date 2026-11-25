#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class StaffInOrificePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x44;
    static constexpr std::string_view NAME = "StaffInOrifice";
    static constexpr uint32_t SIZE = 17;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t playerId = {};
    uint32_t anvilInteractId = {};
    uint32_t itemId = {};
    game::State state = {};
    int16_t unk0x0F = {};

    void Deserialize(BinaryReader& reader) override {
        playerId = reader.ReadUInt32();
        anvilInteractId = reader.ReadUInt32();
        itemId = reader.ReadUInt32();
        state = static_cast<game::State>(reader.ReadInt16());
        unk0x0F = reader.ReadInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](playerId={:#x}, anvilInteractId={:#x}, itemId={:#x}, state={}, unk0x0F={})",
                           Name(), Id(), Size(), playerId, anvilInteractId, itemId, state, unk0x0F);
    }
};

static_assert(sizeof(D2GSPacketClt44) == StaffInOrificePacket::SIZE);

}  // namespace d2bs::lod114d::packets
