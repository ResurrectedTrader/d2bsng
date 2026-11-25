#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

class PlayerStatWordPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x1e;
    static constexpr std::string_view NAME = "PlayerStatWord";
    static constexpr uint32_t SIZE = 4;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    game::Stat stat = {};
    uint16_t value = {};

    void Deserialize(BinaryReader& reader) override {
        stat = static_cast<game::Stat>(reader.ReadByte());
        value = reader.ReadUInt16();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](stat={}, value={})", Name(), Id(), Size(), stat, value);
    }
};

static_assert(sizeof(D2GSPacketSrv1E) == PlayerStatWordPacket::SIZE);

}  // namespace d2bs::lod114d::packets
