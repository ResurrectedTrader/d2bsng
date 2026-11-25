#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class CorpseAssignPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x8e;
    static constexpr std::string_view NAME = "CorpseAssign";
    static constexpr uint32_t SIZE = 10;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t type = {};
    uint32_t playerId = {};
    uint32_t corpseId = {};

    void Deserialize(BinaryReader& reader) override {
        type = reader.ReadByte();
        playerId = reader.ReadUInt32();
        corpseId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](type={}, playerId={:#x}, corpseId={:#x})", Name(), Id(), Size(), type,
                           playerId, corpseId);
    }
};

static_assert(sizeof(D2GSPacketSrv8E) == CorpseAssignPacket::SIZE);

}  // namespace d2bs::lod114d::packets
