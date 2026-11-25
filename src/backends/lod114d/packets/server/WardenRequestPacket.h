#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A Warden request (1.14d only): a 16-bit length and that many encrypted bytes, which are not printed. A length above
// 0x1FD carries no data, as the game sizes it (SERVER_GetServerPacketSize, Game.exe 0x52BAB4).
class WardenRequestPacket : public Packet {
   public:
    static constexpr uint8_t ID = 0xAE;
    static constexpr std::string_view NAME = "WardenRequest";
    static constexpr uint16_t MAX_DATA_SIZE = 0x1FD;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return 3 + DataSize(); }
    bool IsVariableSize() const override { return true; }

    uint16_t dataSize = 0;

    [[nodiscard]] uint32_t DataSize() const { return dataSize > MAX_DATA_SIZE ? 0 : dataSize; }

    void Deserialize(BinaryReader& reader) override {
        dataSize = reader.ReadUInt16();
        reader.ReadSpan(DataSize());
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](dataSize={})", Name(), Id(), Size(), dataSize);
    }
};

}  // namespace d2bs::lod114d::packets
