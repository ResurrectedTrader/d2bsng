#include "components/characterstate/Fingerprint.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "components/characterstate/UnitJson.h"

namespace d2bs::js::characterstate {

namespace {

// Hashing sink for VisitUnit. Absolute value is irrelevant - only that any change to a
// visited field changes the result. Keys are ignored: the visit order is fixed, so the
// value stream alone identifies each field. Structural events (array/element brackets) are
// hashed as marker bytes so a variable-length run - an item's 0..6 sockets - cannot alias a
// different shape.
class HashVisitor final : public UnitVisitor {
   public:
    void Int(std::string_view /*key*/, int64_t value) override {
        Mark(Marker::Int);
        Bytes(&value, sizeof value);
    }
    void Str(std::string_view /*key*/, std::string_view value) override {
        Mark(Marker::Str);
        const size_t len = value.size();
        Bytes(&len, sizeof len);
        Bytes(value.data(), value.size());
    }
    void Ints(std::string_view /*key*/, std::span<const uint16_t> values) override {
        Mark(Marker::Ints);
        const size_t count = values.size();
        Bytes(&count, sizeof count);
        Bytes(values.data(), values.size_bytes());
    }
    void BeginArray(std::string_view /*key*/) override { Mark(Marker::ArrayBegin); }
    void BeginElement() override { Mark(Marker::ElementBegin); }
    void EndElement() override { Mark(Marker::ElementEnd); }
    void EndArray() override { Mark(Marker::ArrayEnd); }

    // Folds an out-of-band scalar (a container's grid dims) into the same hash.
    void Raw(uint32_t value) {
        Mark(Marker::Raw);
        Bytes(&value, sizeof value);
    }

    [[nodiscard]] size_t Result() const { return static_cast<size_t>(hash_); }

   private:
    enum class Marker : uint8_t { Int = 1, Str, Ints, ArrayBegin, ElementBegin, ElementEnd, ArrayEnd, Raw };

    void Mark(Marker marker) { Byte(static_cast<uint8_t>(marker)); }
    void Byte(uint8_t b) { hash_ = (hash_ ^ b) * FNV_PRIME; }
    void Bytes(const void* data, size_t count) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < count; ++i) {
            Byte(bytes[i]);
        }
    }

    static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    uint64_t hash_ = FNV_OFFSET_BASIS;
};

}  // namespace

size_t ContainerHash(const std::vector<game::Unit>& items, game::Size dims) {
    HashVisitor visitor;
    visitor.Raw(dims.width);
    visitor.Raw(dims.height);
    for (const auto& item : items) {
        VisitUnit(visitor, item, Detail::Structural);
    }
    return visitor.Result();
}

size_t UnitHash(const game::Unit& unit) {
    HashVisitor visitor;
    VisitUnit(visitor, unit, Detail::Structural);
    return visitor.Result();
}

}  // namespace d2bs::js::characterstate
