#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class EntityActionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x38;
    static constexpr std::string_view NAME = "EntityAction";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t action = {};
    uint32_t npcId = {};
    uint32_t itemId = {};

    void Deserialize(BinaryReader& reader) override {
        action = reader.ReadUInt32();
        npcId = reader.ReadUInt32();
        itemId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](action={}, npcId={:#x}, itemId={:#x})", Name(), Id(), Size(), action, npcId,
                           itemId);
    }
};

static_assert(sizeof(D2GSPacketClt38) == EntityActionPacket::SIZE);

}  // namespace d2bs::lod114d::packets
