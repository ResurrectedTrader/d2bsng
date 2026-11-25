#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class ChangeTownPortalLocationPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x45;
    static constexpr std::string_view NAME = "ChangeTownPortalLocation";
    static constexpr uint32_t SIZE = 9;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t portalId = {};
    game::LevelId levelId = {};

    void Deserialize(BinaryReader& reader) override {
        portalId = reader.ReadUInt32();
        levelId = static_cast<game::LevelId>(reader.ReadInt32());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](portalId={:#x}, levelId={})", Name(), Id(), Size(), portalId, levelId);
    }
};

static_assert(sizeof(D2GSPacketClt45) == ChangeTownPortalLocationPacket::SIZE);

}  // namespace d2bs::lod114d::packets
