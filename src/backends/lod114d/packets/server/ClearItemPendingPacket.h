#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Takes the uninterruptable state (54) and the stat list that came with it off an item (the client's handler at
// Game.exe 0x45E910 calls CITEMS_ClearPendingState). The type field is the item's unit type.
class ClearItemPendingPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x7c;
    static constexpr std::string_view NAME = "ClearItemPending";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t itemId = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        itemId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, itemId={:#x})", Name(), Id(), Size(), unitType, itemId);
    }
};

static_assert(sizeof(D2GSPacketSrv7C) == ClearItemPendingPacket::SIZE);

}  // namespace d2bs::lod114d::packets
