#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// Empties the NPC's list of hirelings for hire before 0x4E entries refill it (the client's handler at Game.exe
// 0x45E3C0 calls CNPCMENU_ClearHirelingList).
class ClearMercenaryListPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x4F;
    static constexpr std::string_view NAME = "ClearMercenaryList";
    static constexpr uint32_t SIZE = 1;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    void Deserialize(BinaryReader& /*reader*/) override {}

    std::string ToString() const override { return std::format("{}[{:#x}, {}]", Name(), Id(), Size()); }
};

static_assert(sizeof(D2GSPacketSrv4F) == ClearMercenaryListPacket::SIZE);

}  // namespace d2bs::lod114d::packets