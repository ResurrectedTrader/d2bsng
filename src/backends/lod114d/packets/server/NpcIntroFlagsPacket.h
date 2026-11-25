#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class NpcIntroFlagsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x91;
    static constexpr std::string_view NAME = "NpcIntroFlags";
    static constexpr uint32_t SIZE = 26;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Act act = {};
    std::array<uint8_t, 24> npcFlags;

    void Deserialize(BinaryReader& reader) override {
        act = static_cast<game::Act>(reader.ReadByte());
        reader.ReadInto(npcFlags);
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](act={}, npcFlags={})", Name(), Id(), Size(), act, ToHexString(npcFlags));
    }
};

static_assert(sizeof(D2GSPacketSrv91) == NpcIntroFlagsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
