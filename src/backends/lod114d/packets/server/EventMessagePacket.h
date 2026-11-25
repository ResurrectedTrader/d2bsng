#pragma once

#include <array>
#include <utility>

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A game event notice - a player joined, left or was slain, a relation changed (D2GAME_PACKETS_SendPacket0x5A). The
// types up to DiabloClone share one layout; the later ones are printed as bytes.
class EventMessagePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x5A;
    static constexpr std::string_view NAME = "EventMessage";
    static constexpr uint32_t SIZE = 40;
    static constexpr uint32_t NAME_SIZE = 16;

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return SIZE; }

    EventMessageType type = {};
    uint8_t color = 0;
    uint32_t param = 0;
    uint8_t param2 = 0;
    std::string name1;
    std::string name2;
    std::array<uint8_t, SIZE - 2> payload = {};

    [[nodiscard]] bool HasNames() const {
        return std::to_underlying(type) <= std::to_underlying(EventMessageType::DiabloClone);
    }

    void Deserialize(BinaryReader& reader) override {
        type = static_cast<EventMessageType>(reader.ReadByte());
        if (!HasNames()) {
            reader.ReadInto(payload);
            return;
        }
        color = reader.ReadByte();
        param = reader.ReadUInt32();
        param2 = reader.ReadByte();
        name1 = reader.ReadString(NAME_SIZE);
        name2 = reader.ReadString(NAME_SIZE);
    }

    std::string ToString() const override {
        if (!HasNames()) {
            return std::format("{}[{:#x}, {}](type={}, payload={})", Name(), Id(), Size(), type, ToHexString(payload));
        }
        return std::format("{}[{:#x}, {}](type={}, color={}, param={:#x}, param2={}, name1={}, name2={})", Name(), Id(),
                           Size(), type, color, param, param2, name1, name2);
    }
};

static_assert(sizeof(D2GSPacketSrv5A) == EventMessagePacket::SIZE);

}  // namespace d2bs::lod114d::packets
