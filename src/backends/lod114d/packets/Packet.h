#pragma once

// Base of every 1.14d game packet the logger decodes. Each packet class owns its fields, how they are read and how
// they print; the id byte and the length check are handled here, so every packet is parsed the same way.
//
// The field layouts come from D2MOO's D2PacketDef.h, which every fixed-size packet header checks its size against, and
// the ids and sizes from the game's own tables (D2MOO's gServerPacketSizeTable114d / gClientPacketSizeTable114d),
// which the registry checks every fixed-size class against.

#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

// ReSharper disable once CppUnusedIncludeDirective - the D2MOO layouts every packet header static_asserts against
#include <D2PacketDef.h>

#include "game/Types.h"
#include "packets/Binary.h"
#include "packets/Enums.h"
#include "packets/Types.h"

namespace d2bs::lod114d::packets {

// "0x01, 0x02, ..." with trailing zero bytes dropped, so a fixed-size field of mostly padding stays short.
std::string ToHexString(std::span<const uint8_t> bytes);

// The bytes as space-separated pairs ("01 02 ..."), every byte kept.
std::string ToHexDump(std::span<const uint8_t> bytes);

// "a, b, c" for the repeated groups a packet prints.
template <typename Range, typename Format>
std::string Join(const Range& items, const Format& format) {
    std::string text;
    for (const auto& item : items) {
        if (!text.empty()) {
            text += ", ";
        }
        text += format(item);
    }
    return text;
}

class Packet {
   public:
    Packet() = default;
    Packet(const Packet&) = default;
    Packet(Packet&&) = default;
    Packet& operator=(const Packet&) = default;
    Packet& operator=(Packet&&) = default;
    virtual ~Packet() = default;

    [[nodiscard]] virtual uint8_t Id() const = 0;
    [[nodiscard]] virtual std::string_view Name() const = 0;

    // Total wire size including the id byte. For a variable-size packet it is only meaningful once deserialized.
    [[nodiscard]] virtual uint32_t Size() const = 0;

    // Reads the fields after the id byte.
    virtual void Deserialize(BinaryReader& reader) = 0;

    [[nodiscard]] virtual std::string ToString() const = 0;

    [[nodiscard]] virtual bool IsVariableSize() const { return false; }

    // Parses `bytes` (starting at the id byte). False on a malformed packet, including one whose parse consumed a
    // different number of bytes than its Size() says.
    bool FromBytes(std::span<const uint8_t> bytes);
};

}  // namespace d2bs::lod114d::packets
