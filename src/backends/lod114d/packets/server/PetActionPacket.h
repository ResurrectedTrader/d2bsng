#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PetActionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x7a;
    static constexpr std::string_view NAME = "PetAction";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    bool add = {};
    uint8_t petType = {};
    uint16_t monStatsId = {};
    uint32_t ownerId = {};
    uint32_t petId = {};

    void Deserialize(BinaryReader& reader) override {
        add = reader.ReadBoolean();
        petType = reader.ReadByte();
        monStatsId = reader.ReadUInt16();
        ownerId = reader.ReadUInt32();
        petId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](add={}, petType={}, monStatsId={}, ownerId={:#x}, petId={:#x})", Name(), Id(),
                           Size(), add, petType, monStatsId, ownerId, petId);
    }
};

static_assert(sizeof(D2GSPacketSrv7A) == PetActionPacket::SIZE);

}  // namespace d2bs::lod114d::packets
