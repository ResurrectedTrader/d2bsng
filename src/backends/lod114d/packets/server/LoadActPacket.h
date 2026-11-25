#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class LoadActPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x03;
    static constexpr std::string_view NAME = "LoadAct";
    static constexpr uint32_t SIZE = 12;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Act act = {};
    uint32_t gameSeed = {};
    game::LevelId townLevel = {};
    uint32_t objectSeed = {};

    void Deserialize(BinaryReader& reader) override {
        act = static_cast<game::Act>(reader.ReadByte());
        gameSeed = reader.ReadUInt32();
        townLevel = static_cast<game::LevelId>(reader.ReadUInt16());
        objectSeed = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](act={}, gameSeed={:#x}, townLevel={}, objectSeed={:#x})", Name(), Id(),
                           Size(), act, gameSeed, townLevel, objectSeed);
    }
};

static_assert(sizeof(D2GSPacketSrv03) == LoadActPacket::SIZE);

}  // namespace d2bs::lod114d::packets
