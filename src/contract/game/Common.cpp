#include "game/Common.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "game/Constants.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/Unit.h"

namespace d2bs::game {

namespace {

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> AffixNames(
    const Unit& item, const std::array<uint16_t, Unit::MAX_AFFIX_SLOTS>& codes) {
    std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> out;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by loop
    for (size_t i = 0; i < Unit::MAX_AFFIX_SLOTS; ++i) {
        out[i] = item.MagicAffixName(codes[i]);
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    return out;
}

}  // namespace

uint32_t PricingNpcClassId(uint32_t npcClassId) {
    const auto inventory = GetTxtValue("monstats", npcClassId, "inventory");
    const auto* row = std::get_if<int64_t>(&inventory);
    return row == nullptr || *row == 0 ? NPC_CHARSI_CLASS_ID : npcClassId;
}

bool IsWaypointLevel(LevelId levelId) {
    constexpr int64_t NO_WAYPOINT = 255;
    const auto waypoint = GetTxtValue("levels", std::to_underlying(levelId), "Waypoint");
    const auto* index = std::get_if<int64_t>(&waypoint);
    return index == nullptr || *index != NO_WAYPOINT;
}

std::optional<uint16_t> SkillNameStringId(Skill skill) {
    const auto descRow = GetTxtValue("skills", std::to_underlying(skill), "skilldesc");
    const auto* desc = std::get_if<int64_t>(&descRow);
    if (desc == nullptr) {
        return std::nullopt;
    }
    const auto strName = GetTxtValue("skilldesc", static_cast<uint32_t>(*desc), "str name");
    const auto* str = std::get_if<int64_t>(&strName);
    if (str == nullptr) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(*str);
}

std::string Unit::Prefix() const {
    return MagicAffixName(PrefixNum()).value_or(std::string{});
}

std::string Unit::Suffix() const {
    return MagicAffixName(SuffixNum()).value_or(std::string{});
}

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> Unit::Prefixes() const {
    GameReadLock guard;
    return AffixNames(*this, PrefixNums());
}

std::array<std::optional<std::string>, Unit::MAX_AFFIX_SLOTS> Unit::Suffixes() const {
    GameReadLock guard;
    return AffixNames(*this, SuffixNums());
}

}  // namespace d2bs::game
