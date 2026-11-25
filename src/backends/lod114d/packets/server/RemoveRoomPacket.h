#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class RemoveRoomPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x08;
    static constexpr std::string_view NAME = "RemoveRoom";
    static constexpr uint32_t SIZE = 6;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    Point16 position = {};
    game::LevelId levelId = {};

    void Deserialize(BinaryReader& reader) override {
        position = reader.Read<Point16>();
        levelId = static_cast<game::LevelId>(reader.ReadByte());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](position=({}, {}), levelId={})", Name(), Id(), Size(), position.x, position.y,
                           levelId);
    }
};

static_assert(sizeof(D2GSPacketSrv08) == RemoveRoomPacket::SIZE);

}  // namespace d2bs::lod114d::packets
