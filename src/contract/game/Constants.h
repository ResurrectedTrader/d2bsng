#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "ContractEnumNames.h"

namespace d2bs::game {

// Fractional bits of the fixed-point stats (IsFixedPointStat).
constexpr uint32_t STAT_FIXED_POINT_SHIFT = 8;

// Default NPC class ID for pricing (Charsi).
constexpr uint32_t NPC_CHARSI_CLASS_ID = 0x9A;

// Object `InteractType` packs the chest-locked bit at 0x80; the low 7 bits are
// the chest type id.
constexpr uint8_t CHEST_LOCKED_BIT = 0x80;

// Reference parity: a monster's enchants live in the first 9 bytes of its
// unique-mod array (D2MOO nMonUmod), which is 10 bytes wide for alignment.
constexpr size_t ENCHANT_SLOT_COUNT = 9;

// Highest waypoint id accepted by the waypoint table.
constexpr uint32_t MAX_WAYPOINT_ID = 40;

// RosterUnit::wPartyId sentinel for "not in a party".
constexpr uint16_t NO_PARTY_ID = std::numeric_limits<uint16_t>::max();

// Character flags (me.charflags; 1.14d's BnetData::nCharFlags, a 16-bit word).
// Values follow D2MOO's CLIENTSAVEFLAG_*; the bits D2MOO leaves unnamed are
// named after later game versions.
/// The current character's flag bits, as `me.charflags` reports them.
/// @flags
enum class CharFlag : uint16_t {
    Newbie = 0x0001,
    Error = 0x0002,  // set at character creation for realm characters
    Hardcore = 0x0004,
    Dead = 0x0008,
    SaveProcess = 0x0010,
    Expansion = 0x0020,
    Ladder = 0x0040,
    NeedsRenaming = 0x0080,
    ProgressionMask = 0x1F00,  // acts completed across all difficulties
    WeaponSwitch = 0x2000,
};

// Mercenary class IDs (dwTxtFileNo) - used to filter summoned monsters
// when identifying a player's hired merc. Values from reference/d2bs/Constants.h.
constexpr uint32_t MERC_CLASS_ID_ACT1 = 0x010f;
constexpr uint32_t MERC_CLASS_ID_ACT2 = 0x0152;
constexpr uint32_t MERC_CLASS_ID_ACT3 = 0x0167;
constexpr uint32_t MERC_CLASS_ID_ACT5 = 0x0231;

}  // namespace d2bs::game
