#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Every state a unit is in, when it comes into view (D2GAME_STATES_SendUnitStates); the state stream prints as bytes.
class UnitStatesPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xAA;
    static constexpr std::string_view NAME = "UnitStates";
    // id, unit type, unit id, size.
    static constexpr uint32_t HEADER_SIZE = 7;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    game::UnitType unitType = {};
    uint32_t unitId = 0;
    uint8_t packetSize = 0;
    std::string stream;

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        packetSize = reader.ReadByte();
        if (packetSize < HEADER_SIZE) {
            throw IoException("unit states packet shorter than its header");
        }
        stream = ToHexString(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, stream=[{}])", Name(), Id(), Size(), unitType,
                           unitId, stream);
    }
};

}  // namespace d2bs::lod114d::packets
