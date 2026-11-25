#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ObjectModePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x0e;
    static constexpr std::string_view NAME = "ObjectMode";
    static constexpr uint32_t SIZE = 12;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::UnitType unitType = {};
    uint32_t unitId = {};
    uint8_t message = {};
    bool targetable = {};
    game::ObjectMode mode = {};

    void Deserialize(BinaryReader& reader) override {
        unitType = static_cast<game::UnitType>(reader.ReadByte());
        unitId = reader.ReadUInt32();
        message = reader.ReadByte();
        targetable = reader.ReadBoolean();
        mode = static_cast<game::ObjectMode>(reader.ReadUInt32());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitType={}, unitId={:#x}, message={}, targetable={}, mode={})", Name(), Id(),
                           Size(), unitType, unitId, message, targetable, mode);
    }
};

static_assert(sizeof(D2GSPacketSrv0E) == ObjectModePacket::SIZE);

}  // namespace d2bs::lod114d::packets
