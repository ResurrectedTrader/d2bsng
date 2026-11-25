#include "packets/Packet.h"

#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>

namespace d2bs::lod114d::packets {

std::string ToHexString(std::span<const uint8_t> bytes) {
    size_t length = bytes.size();
    while (length > 0 && bytes[length - 1] == 0) {
        --length;
    }
    std::string text;
    for (const uint8_t byte : bytes.first(length)) {
        if (!text.empty()) {
            text += ", ";
        }
        std::format_to(std::back_inserter(text), "{:#04x}", byte);
    }
    return text;
}

std::string ToHexDump(std::span<const uint8_t> bytes) {
    std::string text;
    text.reserve(bytes.size() * 3);
    for (const uint8_t byte : bytes) {
        if (!text.empty()) {
            text += ' ';
        }
        std::format_to(std::back_inserter(text), "{:02x}", byte);
    }
    return text;
}

bool Packet::FromBytes(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes[0] != Id()) {
        return false;
    }
    try {
        BinaryReader reader{bytes.subspan(1)};
        Deserialize(reader);
        return reader.Position() + 1 == Size() && bytes.size() == Size();
    } catch (const IoException&) {
        return false;
    }
}

}  // namespace d2bs::lod114d::packets
