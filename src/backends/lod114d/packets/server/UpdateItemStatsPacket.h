#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// An item's stat changed - durability, quantity, charges (D2GAME_PACKETS_SendPacket0x3E); the stat stream prints as
// bytes.
class UpdateItemStatsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x3E;
    static constexpr std::string_view NAME = "UpdateItemStats";
    // id, size.
    static constexpr uint32_t HEADER_SIZE = 2;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    uint8_t packetSize = 0;
    std::string stream;

    void Deserialize(BinaryReader& reader) override {
        packetSize = reader.ReadByte();
        if (packetSize < HEADER_SIZE) {
            throw IoException("item stat update shorter than its header");
        }
        stream = ToHexString(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](stream=[{}])", Name(), Id(), Size(), stream);
    }
};

}  // namespace d2bs::lod114d::packets
