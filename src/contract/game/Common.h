#pragma once

// Game-version-agnostic helpers the backends share, composed over contract
// primitives (data-table reads). Defined in Common.cpp, which also holds the
// composed handle methods that are not finders (Unit::Prefixes and friends);
// a port implements nothing here.

#include <cstdint>
#include <optional>

namespace d2bs::game {

// Reference parity: an NPC class without a monstats inventory prices as Charsi,
// so the game is never handed a class id it has no price list for.
uint32_t PricingNpcClassId(uint32_t npcClassId);

// Reference parity: levels.txt carries 255 in `Waypoint` for a level no
// waypoint reaches, and sending the game there can take it down. A row the
// lookup cannot read as a number is not rejected.
bool IsWaypointLevel(uint32_t levelId);

// String-table index of a skill's display name, through the skills ->
// skilldesc -> `str name` chain the client itself uses. nullopt when a link
// of the chain is missing.
std::optional<uint16_t> SkillNameStringId(uint16_t skillId);

}  // namespace d2bs::game
