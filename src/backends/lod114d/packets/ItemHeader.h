#pragma once

// The head of the item bitstream a server item packet (0x9C / 0x9D) carries: what ITEMS_SerializeItem (Game.exe
// 0x6312B0, client form) writes before the item's stats - its flags, format, mode and where it is, then its code (or,
// for an ear, the ear's owner). The stats that follow are not decoded.

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "game/Types.h"
#include "packets/Types.h"

namespace d2bs::lod114d::packets {

struct ItemHeader {
    game::ItemFlag flags = {};
    uint32_t format = 0;
    game::ItemMode mode = {};

    // On the ground (or falling to it): its map position. Otherwise where it is held.
    Point16 groundPosition = {};
    game::BodyLocation bodyLocation = {};
    uint8_t gridX = 0;
    uint8_t gridY = 0;
    // The inventory page plus one, as written; 0 is no page.
    uint8_t storedPage = 0;

    // The four-character base code, trailing spaces dropped; empty for an ear.
    std::string code;
    uint8_t earClass = 0;
    uint8_t earLevel = 0;
    std::string earName;
    // The amount, for a compact-saved gold pile.
    std::optional<uint32_t> gold;

    // Throws IoException on a stream shorter than the header.
    static ItemHeader Parse(std::span<const uint8_t> bitstream);

    [[nodiscard]] bool IsOnGround() const;
    [[nodiscard]] std::string ToString() const;
};

}  // namespace d2bs::lod114d::packets
