#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A monster or NPC comes into view, with its mode and modifiers in a trailing bitstream that prints as bytes.
class AssignMonsterPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xAC;
    static constexpr std::string_view NAME = "AssignMonster";
    // id, unit id, class, position, life, size.
    static constexpr uint32_t HEADER_SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return packetSize; }
    bool IsVariableSize() const override { return true; }

    uint32_t unitId = 0;
    uint16_t classId = 0;
    Point16 position = {};
    uint8_t lifePercent = 0;
    uint8_t packetSize = 0;
    std::string bitstream;

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        classId = reader.ReadUInt16();
        position = reader.Read<Point16>();
        lifePercent = reader.ReadByte();
        packetSize = reader.ReadByte();
        if (packetSize < HEADER_SIZE) {
            throw IoException("monster packet shorter than its header");
        }
        bitstream = ToHexString(reader.ReadSpan(packetSize - HEADER_SIZE));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, classId={}, position=({}, {}), lifePercent={}, bitstream=[{}])",
                           Name(), Id(), Size(), unitId, classId, position.x, position.y, lifePercent, bitstream);
    }
};

}  // namespace d2bs::lod114d::packets
