#include "packets/Binary.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>

namespace d2bs::lod114d::packets {

const uint8_t* BinaryReader::Take(uint32_t count) {
    if (count > BytesRemaining()) {
        throw IoException(
            std::format("read of {} bytes at {} runs past the {}-byte packet", count, position_, data_.size()));
    }
    const auto* start = data_.subspan(position_).data();
    position_ += count;
    return start;
}

std::string BinaryReader::ReadString(uint32_t size) {
    if (size != 0) {
        const auto field = ReadSpan(size);
        const auto end = std::ranges::find(field, uint8_t{0});
        return {field.begin(), end};
    }
    std::string text;
    for (uint8_t c = ReadByte(); c != 0; c = ReadByte()) {
        text.push_back(static_cast<char>(c));
    }
    return text;
}

uint32_t BitReader::ReadBits(uint32_t count) {
    if (count > 32 || count > BitsRemaining()) {
        throw IoException(
            std::format("read of {} bits at bit {} runs past the {}-byte buffer", count, position_, data_.size()));
    }
    uint32_t value = 0;
    for (uint32_t i = 0; i < count; ++i, ++position_) {
        const uint8_t byte = data_[position_ / 8];
        value |= static_cast<uint32_t>((byte >> (position_ % 8)) & 1U) << i;
    }
    return value;
}

}  // namespace d2bs::lod114d::packets
