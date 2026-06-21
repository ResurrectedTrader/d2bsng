#pragma once

#include <cstdint>

namespace d2bs::game {

// Stat IDs referenced in the API layer.
// Values from reference/d2bs/Constants.h and JSUnit.cpp.
constexpr uint32_t STAT_EXP = 13;
constexpr uint32_t STAT_LASTEXP = 29;
constexpr uint32_t STAT_NEXTEXP = 30;
constexpr uint32_t STAT_ITEMLEVELREQ = 92;
constexpr uint32_t STAT_FIXED_POINT_SHIFT = 8;
constexpr uint32_t STAT_LIST_PRESET_FLAG = 0x40;

// Default NPC class ID for pricing (Charsi).
constexpr uint32_t NPC_CHARSI_CLASS_ID = 0x9A;

// Character flags (BnetData::nCharFlags). Reference Constants.h PLAYER_TYPE_*.
// Surfaced in the API docs for me.charflags; test against the raw nCharFlags
// value with std::to_underlying(CharFlag::X).
/// @flags
enum class CharFlag : uint32_t {
    Hardcore = 0x04,
    Expansion = 0x20,
    Ladder = 0x40,
};

// Mercenary class IDs (dwTxtFileNo) - used to filter summoned monsters
// when identifying a player's hired merc. Values from reference/d2bs/Constants.h.
constexpr uint32_t MERC_CLASS_ID_ACT1 = 0x010f;
constexpr uint32_t MERC_CLASS_ID_ACT2 = 0x0152;
constexpr uint32_t MERC_CLASS_ID_ACT3 = 0x0167;
constexpr uint32_t MERC_CLASS_ID_ACT5 = 0x0231;

}  // namespace d2bs::game
