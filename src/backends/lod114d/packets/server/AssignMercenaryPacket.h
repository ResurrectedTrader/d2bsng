#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class AssignMercenaryPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x81;
    static constexpr std::string_view NAME = "AssignMercenary";
    static constexpr uint32_t SIZE = 20;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint8_t petType = {};
    uint16_t monStatsId = {};
    uint32_t ownerId = {};
    uint32_t petId = {};
    uint32_t nameSeed = {};
    uint32_t nameStringId = {};

    void Deserialize(BinaryReader& reader) override {
        petType = reader.ReadByte();
        monStatsId = reader.ReadUInt16();
        ownerId = reader.ReadUInt32();
        petId = reader.ReadUInt32();
        nameSeed = reader.ReadUInt32();
        nameStringId = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format(
            "{}[{:#x}, {}](petType={}, monStatsId={}, ownerId={:#x}, petId={:#x}, nameSeed={:#x}, nameStringId={:#x})",
            Name(), Id(), Size(), petType, monStatsId, ownerId, petId, nameSeed, nameStringId);
    }
};

static_assert(sizeof(D2GSPacketSrv81) == AssignMercenaryPacket::SIZE);

}  // namespace d2bs::lod114d::packets
