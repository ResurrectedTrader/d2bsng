#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A state turns on with the stats it grants (D2GAME_STATES_SendUnitStateUpdates); the stat stream prints as bytes.
class SetStateWithStatsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xA8;
    static constexpr std::string_view NAME = "SetStateWithStats";
    // id, unit type, unit id, size, state.
    static constexpr uint32_t HEADER_SIZE = 8;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    game::UnitType unitType = {};
    uint32_t unitId = 0;
    uint8_t packetSize = 0;
    game::State state = {};
    std::string stream;

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        packetSize = reader.ReadByte();
        state = static_cast<game::State>(reader.ReadByte());
        if (packetSize < HEADER_SIZE) {
            throw IoException("state packet shorter than its header");
        }
        stream = ToHexString(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, state={}, stream=[{}])", Name(), Id(), Size(),
                           unitType, unitId, state, stream);
    }
};

}  // namespace d2bs::lod114d::packets
