#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// The client's ping (D2CLTSYS_PING). 1.14d sends 13 bytes where 1.10f sent 9; CLIENTS_UpdatePing reads the first two
// fields, and the last word is printed as it is.
class PingPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x6D;
    static constexpr std::string_view NAME = "Ping";
    static constexpr uint32_t SIZE = 13;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    uint32_t tickCount = 0;
    uint32_t halfAveragePing = 0;
    uint32_t unk0x09 = 0;

    void Deserialize(BinaryReader& reader) override {
        tickCount = reader.ReadUInt32();
        halfAveragePing = reader.ReadUInt32();
        unk0x09 = reader.ReadUInt32();
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](tickCount={}, halfAveragePing={}, unk0x09={:#x})", Name(), Id(), Size(),
                           tickCount, halfAveragePing, unk0x09);
    }
};

}  // namespace d2bs::lod114d::packets
