#pragma once

// Game-version-agnostic helpers the backends share, composed over contract
// primitives (data-table reads). Defined in Common.cpp, which also holds the
// composed handle methods that are not finders (Unit::Prefixes and friends);
// a port implements nothing here.

#include <cstdint>
#include <optional>
#include <string_view>

#include "game/Types.h"

namespace d2bs::game {

// Reference parity: an NPC class without a monstats inventory prices as Charsi,
// so the game is never handed a class id it has no price list for.
uint32_t PricingNpcClassId(uint32_t npcClassId);

// Reference parity: levels.txt carries 255 in `Waypoint` for a level no
// waypoint reaches, and sending the game there can take it down. A row the
// lookup cannot read as a number is not rejected.
bool IsWaypointLevel(LevelId levelId);

// String-table index of a skill's display name, through the skills ->
// skilldesc -> `str name` chain the client itself uses. nullopt when a link
// of the chain is missing.
std::optional<uint16_t> SkillNameStringId(Skill skill);

// A skill id by name, case-insensitively, from the 1.14d skill names scripts were
// written against (reference D2Skills.h). The backends' GetSkillByName answers
// from it while the legacySkillNames compatibility flag is on.
std::optional<Skill> LegacySkillByName(std::string_view name);

}  // namespace d2bs::game
