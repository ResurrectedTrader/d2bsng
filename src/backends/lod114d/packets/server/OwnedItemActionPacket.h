#pragma once

#include <optional>

#include "packets/ItemHeader.h"
#include "packets/Packet.h"
#include "packets/server/ItemActionPacket.h"

namespace d2bs::lod114d::packets {

// An item a unit holds changes: equipped, unequipped, swapped, moved in a belt, its stats updated.
class OwnedItemActionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x9D;
    static constexpr std::string_view NAME = "OwnedItemAction";
    // id, action, size, component, item id, owner type, owner id.
    static constexpr uint32_t HEADER_SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    ItemAction action = {};
    uint8_t packetSize = 0;
    uint8_t component = 0;
    uint32_t itemId = 0;
    game::UnitType ownerType = {};
    uint32_t ownerId = 0;
    std::optional<ItemHeader> item;

    void Deserialize(BinaryReader& reader) override {
        action = static_cast<ItemAction>(reader.ReadByte());
        packetSize = reader.ReadByte();
        component = reader.ReadByte();
        itemId = reader.ReadUInt32();
        ownerType = static_cast<game::UnitType>(reader.ReadByte());
        ownerId = reader.ReadUInt32();
        if (packetSize < HEADER_SIZE) {
            throw IoException("item packet shorter than its header");
        }
        item = ItemActionPacket::ParseItem(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](action={}, component={}, itemId={:#x}, ownerType={}, ownerId={:#x}, {})",
                           Name(), Id(), Size(), action, component, itemId, ownerType, ownerId,
                           item.has_value() ? item->ToString() : "item=?");
    }
};

}  // namespace d2bs::lod114d::packets
