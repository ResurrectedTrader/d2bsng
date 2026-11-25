#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignCorpsePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x74;
    static constexpr std::string_view NAME = "AssignCorpse";
    static constexpr uint32_t SIZE = 10;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    bool assign = {};
    uint32_t playerId = {};
    uint32_t corpseId = {};

    void Deserialize(BinaryReader& reader) override {
        assign = reader.ReadBoolean();
        playerId = reader.ReadUInt32();
        corpseId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](assign={}, playerId={:#x}, corpseId={:#x})", Name(), Id(), Size(), assign,
                           playerId, corpseId);
    }
};

static_assert(sizeof(D2GSPacketSrv74) == AssignCorpsePacket::SIZE);

}  // namespace d2bs::lod114d::packets
