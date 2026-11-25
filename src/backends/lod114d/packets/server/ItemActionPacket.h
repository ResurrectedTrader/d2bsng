#pragma once

#include <optional>
#include <span>

#include "packets/ItemHeader.h"
#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// An item the player does not hold changes: dropped, picked up, put in a container or a belt, added to a shop.
class ItemActionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x9C;
    static constexpr std::string_view NAME = "ItemAction";
    // id, action, size, component, item id.
    static constexpr uint32_t HEADER_SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    ItemAction action = {};
    uint8_t packetSize = 0;
    uint8_t component = 0;
    uint32_t itemId = 0;
    std::optional<ItemHeader> item;

    void Deserialize(BinaryReader& reader) override {
        action = static_cast<ItemAction>(reader.ReadByte());
        packetSize = reader.ReadByte();
        component = reader.ReadByte();
        itemId = reader.ReadUInt32();
        if (packetSize < HEADER_SIZE) {
            throw IoException("item packet shorter than its header");
        }
        item = ParseItem(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](action={}, component={}, itemId={:#x}, {})", Name(), Id(), Size(), action,
                           component, itemId, item.has_value() ? item->ToString() : "item=?");
    }

    // A stream too short for the header still frames: the packet prints without it.
    static std::optional<ItemHeader> ParseItem(std::span<const uint8_t> bitstream) {
        try {
            return ItemHeader::Parse(bitstream);
        } catch (const IoException&) {
            return std::nullopt;
        }
    }
};

}  // namespace d2bs::lod114d::packets
