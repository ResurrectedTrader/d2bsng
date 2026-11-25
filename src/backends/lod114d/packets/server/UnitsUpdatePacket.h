#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A batch of unit position updates (D2GAME_PACKETS_SendPacket0x16_UnitsUpdate); the entries print as bytes.
class UnitsUpdatePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x16;
    static constexpr std::string_view NAME = "UnitsUpdate";
    // id, size, count.
    static constexpr uint32_t HEADER_SIZE = 4;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    uint16_t packetSize = 0;
    uint8_t count = 0;
    std::string updates;

    void Deserialize(BinaryReader& reader) override {
        packetSize = reader.ReadUInt16();
        count = reader.ReadByte();
        if (packetSize < HEADER_SIZE) {
            throw IoException("unit update shorter than its header");
        }
        updates = ToHexString(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](count={}, updates=[{}])", Name(), Id(), Size(), count, updates);
    }
};

}  // namespace d2bs::lod114d::packets
