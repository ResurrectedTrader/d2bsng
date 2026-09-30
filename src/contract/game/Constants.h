#pragma once

#include <cstdint>
#include <limits>

// ReSharper disable once CppUnusedIncludeDirective - EnumName / format_as for the enumerations below
#include "ContractEnumNames.h"

namespace d2bs::game {

// Stat IDs referenced in the API layer.
// Values from reference/d2bs/Constants.h and JSUnit.cpp.
constexpr uint32_t STAT_EXP = 13;
constexpr uint32_t STAT_LASTEXP = 29;
constexpr uint32_t STAT_NEXTEXP = 30;
constexpr uint32_t STAT_ITEMLEVELREQ = 92;
constexpr uint32_t STAT_FIXED_POINT_SHIFT = 8;
// hp/mana/stamina (STAT_HITPOINTS..STAT_MAXSTAMINA) are the contiguous run D2 stores
// in 8.8 fixed point.
constexpr uint32_t STAT_FIXED_POINT_FIRST = 6;
constexpr uint32_t STAT_FIXED_POINT_LAST = 11;
constexpr uint32_t STAT_LIST_PRESET_FLAG = 0x40;

// Default NPC class ID for pricing (Charsi).
constexpr uint32_t NPC_CHARSI_CLASS_ID = 0x9A;

// Highest waypoint id accepted by the waypoint table.
constexpr uint32_t MAX_WAYPOINT_ID = 40;

// RosterUnit::wPartyId sentinel for "not in a party".
constexpr uint16_t NO_PARTY_ID = std::numeric_limits<uint16_t>::max();

// Character flags (me.charflags; 1.14d's BnetData::nCharFlags). Values follow
// D2MOO's CLIENTSAVEFLAG_*, which D2R keeps; the names of the bits D2MOO leaves
// unnamed are D2R's.
/// @flags
enum class CharFlag : uint32_t {
    Newbie = 0x0001,
    Error = 0x0002,  // D2MOO: set at character creation for realm characters
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
