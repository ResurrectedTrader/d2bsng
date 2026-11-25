#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignPlayerPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x59;
    static constexpr std::string_view NAME = "AssignPlayer";
    static constexpr uint32_t SIZE = 26;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t unitId = {};
    game::CharacterClass characterClass = {};
    std::string name;
    Point16 position = {};

    void Deserialize(BinaryReader& reader) override {
        unitId = reader.ReadUInt32();
        characterClass = static_cast<game::CharacterClass>(reader.ReadByte());
        name = reader.ReadString(16);
        position = reader.Read<Point16>();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](unitId={:#x}, class={}, name={}, position=({}, {}))", Name(), Id(), Size(),
                           unitId, characterClass, name, position.x, position.y);
    }
};

static_assert(sizeof(D2GSPacketSrv59) == AssignPlayerPacket::SIZE);

}  // namespace d2bs::lod114d::packets
