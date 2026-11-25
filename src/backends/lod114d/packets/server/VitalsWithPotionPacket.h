#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// The vitals of 0x95 plus how far the potions being drunk will take life and mana, bit-packed
// (SCMD_SendVitalsAndPotionPrediction).
class VitalsWithPotionPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x18;
    static constexpr std::string_view NAME = "VitalsWithPotion";
    static constexpr uint32_t SIZE = 15;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t life = 0;
    uint32_t mana = 0;
    uint32_t stamina = 0;
    uint32_t potionLifePercent = 0;
    uint32_t potionManaPercent = 0;
    Point16 position = {};
    uint8_t targetOffsetX = 0;
    uint8_t targetOffsetY = 0;

    void Deserialize(BinaryReader& reader) override {
        BitReader bits{reader.ReadSpan(SIZE - 1)};
        life = bits.ReadBits(15);
        mana = bits.ReadBits(15);
        stamina = bits.ReadBits(15);
        potionLifePercent = bits.ReadBits(7);
        potionManaPercent = bits.ReadBits(7);
        position.x = static_cast<uint16_t>(bits.ReadBits(16));
        position.y = static_cast<uint16_t>(bits.ReadBits(16));
        targetOffsetX = static_cast<uint8_t>(bits.ReadBits(8));
        targetOffsetY = static_cast<uint8_t>(bits.ReadBits(8));
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](life={}, mana={}, stamina={}, potionLifePercent={}, potionManaPercent={}, "
                           "position=({}, {}), targetOffset=({}, {}))",
                           Name(), Id(), Size(), life, mana, stamina, potionLifePercent, potionManaPercent, position.x,
                           position.y, targetOffsetX, targetOffsetY);
    }
};

static_assert(sizeof(D2GSPacketSrv18) == VitalsWithPotionPacket::SIZE);

}  // namespace d2bs::lod114d::packets
