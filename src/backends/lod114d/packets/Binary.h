#pragma once

// Little-endian reads over a packet's bytes, plus a bit cursor for the packets that carry bit-packed fields. The packet
// classes deserialize through these; a read past the end throws IoException, which is how a short packet is told
// apart from a well-formed one.

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace d2bs::lod114d::packets {

static_assert(std::endian::native == std::endian::little, "the wire format is read by memcpy");

class IoException : public std::runtime_error {
   public:
    explicit IoException(const std::string& message) : std::runtime_error(message) {}
};

// Cursor over bytes the caller owns.
class BinaryReader {
   public:
    explicit BinaryReader(std::span<const uint8_t> data) : data_(data) {}

    bool ReadBoolean() { return *Take(1) != 0; }
    uint8_t ReadByte() { return *Take(1); }
    int16_t ReadInt16() { return Read<int16_t>(); }
    uint16_t ReadUInt16() { return Read<uint16_t>(); }
    int32_t ReadInt32() { return Read<int32_t>(); }
    uint32_t ReadUInt32() { return Read<uint32_t>(); }

    // A NUL-terminated string. A non-zero `size` is a fixed-width field: the cursor moves past all of it, and a value
    // that fills it has no terminator.
    std::string ReadString(uint32_t size = 0);

    template <typename T>
    T Read() {
        static_assert(std::is_trivially_copyable_v<T>, "Read<T> copies raw bytes");
        T value;
        std::memcpy(&value, Take(sizeof(T)), sizeof(T));
        return value;
    }

    template <typename T, size_t N>
    void ReadInto(std::array<T, N>& value) {
        static_assert(std::is_trivially_copyable_v<T>, "ReadInto copies raw bytes");
        std::memcpy(value.data(), Take(N * sizeof(T)), N * sizeof(T));
    }

    // The next `count` bytes, as a view into the packet.
    std::span<const uint8_t> ReadSpan(uint32_t count) { return {Take(count), count}; }

    [[nodiscard]] uint32_t BytesRemaining() const { return static_cast<uint32_t>(data_.size()) - position_; }
    [[nodiscard]] uint32_t Position() const { return position_; }

   private:
    const uint8_t* Take(uint32_t count);

    std::span<const uint8_t> data_;
    uint32_t position_ = 0;
};

// Bit cursor over a caller-owned buffer, least-significant bit first within each byte - the order the game's bit
// buffers (BITMANIP_Write) use.
class BitReader {
   public:
    explicit BitReader(std::span<const uint8_t> data) : data_(data) {}

    // Up to 32 bits as an unsigned value.
    uint32_t ReadBits(uint32_t count);

    [[nodiscard]] uint32_t BitsRemaining() const { return static_cast<uint32_t>(data_.size() * 8) - position_; }

   private:
    std::span<const uint8_t> data_;
    uint32_t position_ = 0;
};

}  // namespace d2bs::lod114d::packets
