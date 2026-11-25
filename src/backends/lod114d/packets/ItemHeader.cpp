#include "packets/ItemHeader.h"

#include <cstdint>
#include <format>
#include <span>
#include <string>

#include "game/Types.h"
#include "packets/Binary.h"

namespace d2bs::lod114d::packets {

// Field widths as ITEMS_SerializeItemCompact / ITEMS_SerializeItemComplete write them.
ItemHeader ItemHeader::Parse(std::span<const uint8_t> bitstream) {
    constexpr uint32_t FLAG_BITS = 32;
    constexpr uint32_t FORMAT_BITS = 10;
    constexpr uint32_t MODE_BITS = 3;
    constexpr uint32_t GROUND_COORD_BITS = 16;
    constexpr uint32_t BODY_LOCATION_BITS = 4;
    constexpr uint32_t GRID_COORD_BITS = 4;
    constexpr uint32_t PAGE_BITS = 3;
    constexpr uint32_t EAR_CLASS_BITS = 3;
    constexpr uint32_t EAR_LEVEL_BITS = 7;
    constexpr uint32_t EAR_CHAR_BITS = 7;
    constexpr uint32_t CODE_BITS = 32;
    constexpr uint32_t MAX_EAR_NAME = 16;
    constexpr uint32_t GOLD_SHORT_BITS = 12;
    constexpr uint32_t GOLD_LONG_BITS = 32;
    constexpr std::string_view GOLD_CODE = "gld";

    BitReader bits{bitstream};
    ItemHeader header;
    header.flags = static_cast<game::ItemFlag>(bits.ReadBits(FLAG_BITS));
    header.format = bits.ReadBits(FORMAT_BITS);
    header.mode = static_cast<game::ItemMode>(bits.ReadBits(MODE_BITS));
    if (header.IsOnGround()) {
        header.groundPosition.x = static_cast<uint16_t>(bits.ReadBits(GROUND_COORD_BITS));
        header.groundPosition.y = static_cast<uint16_t>(bits.ReadBits(GROUND_COORD_BITS));
    } else {
        header.bodyLocation = static_cast<game::BodyLocation>(bits.ReadBits(BODY_LOCATION_BITS));
        header.gridX = static_cast<uint8_t>(bits.ReadBits(GRID_COORD_BITS));
        header.gridY = static_cast<uint8_t>(bits.ReadBits(GRID_COORD_BITS));
        header.storedPage = static_cast<uint8_t>(bits.ReadBits(PAGE_BITS));
    }

    if (HasFlag(header.flags, game::ItemFlag::IsEar)) {
        header.earClass = static_cast<uint8_t>(bits.ReadBits(EAR_CLASS_BITS));
        header.earLevel = static_cast<uint8_t>(bits.ReadBits(EAR_LEVEL_BITS));
        for (uint32_t i = 0; i < MAX_EAR_NAME; ++i) {
            const auto c = static_cast<char>(bits.ReadBits(EAR_CHAR_BITS));
            if (c == '\0') {
                break;
            }
            header.earName.push_back(c);
        }
        return header;
    }

    const uint32_t code = bits.ReadBits(CODE_BITS);
    for (uint32_t shift = 0; shift < CODE_BITS; shift += 8) {
        header.code.push_back(static_cast<char>((code >> shift) & 0xFFU));
    }
    while (!header.code.empty() && (header.code.back() == ' ' || header.code.back() == '\0')) {
        header.code.pop_back();
    }
    if (header.code == GOLD_CODE && HasFlag(header.flags, game::ItemFlag::CompactSave)) {
        const bool isLong = bits.ReadBits(1) != 0;
        header.gold = bits.ReadBits(isLong ? GOLD_LONG_BITS : GOLD_SHORT_BITS);
    }
    return header;
}

bool ItemHeader::IsOnGround() const {
    return mode == game::ItemMode::OnGround || mode == game::ItemMode::Dropping;
}

std::string ItemHeader::ToString() const {
    std::string text = std::format("flags={}, format={}, mode={}", flags, format, mode);
    if (IsOnGround()) {
        std::format_to(std::back_inserter(text), ", position=({}, {})", groundPosition.x, groundPosition.y);
    } else {
        std::format_to(std::back_inserter(text), ", bodyLocation={}, grid=({}, {}), page={}", bodyLocation, gridX,
                       gridY, static_cast<int32_t>(storedPage) - 1);
    }
    if (HasFlag(flags, game::ItemFlag::IsEar)) {
        std::format_to(std::back_inserter(text), ", ear=({}, level {}, {})", earClass, earLevel, earName);
    } else {
        std::format_to(std::back_inserter(text), ", code={}", code);
    }
    if (gold.has_value()) {
        std::format_to(std::back_inserter(text), ", gold={}", *gold);
    }
    return text;
}

}  // namespace d2bs::lod114d::packets
