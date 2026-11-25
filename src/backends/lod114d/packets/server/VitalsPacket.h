#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// The player's life, mana and stamina and where the server has it, bit-packed (SCMD_SendVitals, Game.exe 0x53C320).
class VitalsPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x95;
    static constexpr std::string_view NAME = "Vitals";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t life = 0;
    uint32_t mana = 0;
    uint32_t stamina = 0;
    Point16 position = {};
    uint8_t targetOffsetX = 0;
    uint8_t targetOffsetY = 0;

    void Deserialize(BinaryReader& reader) override {
        BitReader bits{reader.ReadSpan(SIZE - 1)};
        life = bits.ReadBits(15);
        mana = bits.ReadBits(15);
        stamina = bits.ReadBits(15);
        position.x = static_cast<uint16_t>(bits.ReadBits(16));
        position.y = static_cast<uint16_t>(bits.ReadBits(16));
        targetOffsetX = static_cast<uint8_t>(bits.ReadBits(8));
        targetOffsetY = static_cast<uint8_t>(bits.ReadBits(8));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](life={}, mana={}, stamina={}, position=({}, {}), targetOffset=({}, {}))",
                           Name(), Id(), Size(), life, mana, stamina, position.x, position.y, targetOffsetX,
                           targetOffsetY);
    }
};

static_assert(sizeof(D2GSPacketSrv95) == VitalsPacket::SIZE);

}  // namespace d2bs::lod114d::packets
