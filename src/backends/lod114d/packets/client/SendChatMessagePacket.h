#pragma once

#include "packets/Packet.h"

namespace d2bs::lod114d::packets {

// A chat line or whisper the player sends. Two NUL-terminated strings - the message and a whisper target - then a byte
// that sizes the trailing data (SERVER_GetClientPacketSize, Game.exe 0x52BCE0).
class SendChatMessagePacket : public Packet {
   public:
    static constexpr uint8_t ID = 0x15;
    static constexpr std::string_view NAME = "SendChatMessage";

    uint8_t Id() const override { return ID; }
    std::string_view Name() const override { return NAME; }
    uint32_t Size() const override { return size_; }
    bool IsVariableSize() const override { return true; }

    uint8_t messageType = 0;
    uint8_t language = 0;
    std::string message;
    std::string target;
    std::string trailer;

    void Deserialize(BinaryReader& reader) override {
        messageType = reader.ReadByte();
        language = reader.ReadByte();
        message = reader.ReadString();
        target = reader.ReadString();
        trailer = ToHexString(reader.ReadSpan(reader.ReadByte()));
        size_ = reader.Position() + 1;
    }

    std::string ToString() const override {
        return std::format("{}[{:#x}, {}](messageType={}, language={}, message={}, target={}, trailer=[{}])", Name(),
                           Id(), Size(), messageType, language, message, target, trailer);
    }

   private:
    uint32_t size_ = 0;
};

}  // namespace d2bs::lod114d::packets