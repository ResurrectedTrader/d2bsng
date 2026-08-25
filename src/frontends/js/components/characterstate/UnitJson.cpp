#include "components/characterstate/UnitJson.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "game/Console.h"
#include "game/Constants.h"
// ReSharper disable once CppUnusedIncludeDirective - inline body of Unit::GetItems
#include "game/Finders.h"
#include "game/GameHelpers.h"
#include "game/Types.h"

namespace d2bs::js::characterstate {

namespace {

// NOLINTNEXTLINE(readability-identifier-naming) - 'json' is nlohmann's conventional alias spelling
using json = nlohmann::json;

// Game emits description lines bottom-to-top; reverse them for the manager. A color
// code colours all following text, so a line can inherit colour from an earlier one
// - carry the active code forward (prepend it to any non-empty line lacking its own)
// before reversing. Blank lines are skipped: a code prepended there becomes a
// code-only line the manager echoes verbatim as "yc4".
std::string ReverseLines(const std::string& text) {
    std::vector<std::string> lines;
    {
        const std::string_view view{text};
        size_t start = 0;
        while (true) {
            const size_t nl = view.find('\n', start);
            if (nl == std::string_view::npos) {
                lines.emplace_back(view.substr(start));
                break;
            }
            lines.emplace_back(view.substr(start, nl - start));
            start = nl + 1;
        }
    }

    std::string activeColor;
    for (auto& line : lines) {
        size_t seqLen = 0;
        size_t codeOffset = 0;
        if (!activeColor.empty() && !line.empty() && !game::console::DetectColorSequence(line, 0, seqLen, codeOffset)) {
            line.insert(0, activeColor);
        }
        for (size_t i = 0; i < line.size();) {
            if (game::console::DetectColorSequence(line, i, seqLen, codeOffset)) {
                activeColor = line.substr(i, seqLen);
                i += seqLen;
            } else {
                ++i;
            }
        }
    }

    std::string out;
    out.reserve(text.size());
    for (size_t i = lines.size(); i-- > 0;) {
        out += lines[i];
        if (i != 0) {
            out += '\n';
        }
    }
    return out;
}

// Stat values are stored raw (pre-nValShift) so they stay stable across wearers; undo the
// shift game::Unit::GetStatLists applies to hp/mana/stamina.
int32_t RawValue(const game::StatEntry& stat) {
    if (stat.statId >= game::STAT_FIXED_POINT_FIRST && stat.statId <= game::STAT_FIXED_POINT_LAST) {
        return stat.value << game::STAT_FIXED_POINT_SHIFT;
    }
    return stat.value;
}

json BuildStatLists(const game::Unit& unit) {
    json lists = json::array();
    for (const auto& list : unit.GetStatLists()) {
        json stats = json::array();
        for (const auto& stat : list.stats) {
            json entry = json::object();
            entry["id"] = stat.statId;
            entry["value"] = RawValue(stat);
            if (stat.subIndex != 0) {
                entry["layer"] = stat.subIndex;
            }
            stats.push_back(std::move(entry));
        }
        json group = json::object();
        group["stateNo"] = list.stateNo;
        group["flags"] = list.flags;
        group["stats"] = std::move(stats);
        lists.push_back(std::move(group));
    }
    return lists;
}

void AddItemFields(json& out, const game::Unit& item, Detail detail) {
    // The compiled code is space padded, not NUL padded.
    std::string code = item.ItemCode();
    while (!code.empty() && code.back() == ' ') {
        code.pop_back();
    }
    out["code"] = code;

    out["quality"] = item.Quality();
    out["itemFlags"] = item.ItemFlags();
    out["format"] = item.ItemFormat();
    const auto fileIndex = item.FileIndex();
    out["fileIndex"] = fileIndex.has_value() ? static_cast<int32_t>(*fileIndex) : -1;
    out["rarePrefix"] = item.RarePrefixNum();
    out["rareSuffix"] = item.RareSuffixNum();
    out["autoAffix"] = item.AutoAffixNum();
    out["magicPrefix"] = item.PrefixNums();
    out["magicSuffix"] = item.SuffixNums();
    out["itemLevel"] = item.ItemLevel();
    out["earLevel"] = item.EarLevel();
    out["playerName"] = item.ItemPlayerName();
    // Which of the random inventory graphics this instance rolled, for item types with
    // varinvgfx (rings, amulets, jewels, charms). Nothing else in the document implies
    // it, so a consumer resolving the graphic itself needs it.
    out["gfxIndex"] = item.GfxIndex();

    if (detail == Detail::Full) {
        out["title"] = item.Name();
        // Game tooltip, lines reversed to display order (see ReverseLines).
        out["description"] = ReverseLines(item.Description());
        out["statsLists"] = BuildStatLists(item);
    }

    out["gid"] = item.Id();
    out["location"] = item.ItemLocation();
    // Slot containers (equipped/merc) carry the equip-location in x with y = 0;
    // grid containers carry the cell.
    const auto pos = item.Pos();
    const auto size = item.Size();
    out["x"] = pos.x;
    out["y"] = pos.y;
    out["w"] = size.width;
    out["h"] = size.height;

    // An item's contained units are its socket fillers, and the inventory chain is
    // append-ordered, so chain order is socket order. Fillers are contiguous from 0, so
    // array position is the socket index; the total is stat 194.
    json sockets = json::array();
    for (const auto& filler : item.GetItems()) {
        sockets.push_back(UnitToJson(filler, detail));
    }
    if (!sockets.empty()) {
        out["sockets"] = std::move(sockets);
    }
}

// Curated stat ids the manager's StatsPanel labels (D2BotNG stats.ts). Order is
// irrelevant; the manager renders only ids it recognises.
constexpr std::array<uint32_t, 22> STAT_IDS = {0,  1,  2,  3,  7,  9,  12, 13, 14, 15, 39,
                                               40, 41, 42, 43, 44, 45, 46, 80, 96, 99, 105};

}  // namespace

json WearerStats(const game::Unit& wearer) {
    // itemstatcost flags genuinely-signed stats (resists, etc.) as Signed=1 -> sign-extend;
    // the rest are unsigned -> zero-extend, so 32-bit experience doesn't read negative. The
    // flag is static table data, and resolving it re-scans the table schema, so pair it with
    // each id once. Only latched once every lookup returned a real cell - caching a miss
    // (tables not loaded yet) would render signed stats wrong for the process lifetime.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - game-thread only
    static std::vector<std::pair<uint32_t, bool>> curated;
    if (curated.size() != STAT_IDS.size()) {
        curated.clear();
        for (const uint32_t id : STAT_IDS) {
            const auto cell = game::GetTxtValue("itemstatcost", id, "signed");
            const auto* flag = std::get_if<int64_t>(&cell);
            if (flag == nullptr) {
                curated.clear();
                break;
            }
            curated.emplace_back(id, *flag == 1);
        }
    }

    json stats = json::array();
    for (const auto& [id, isSigned] : curated) {
        const int32_t raw = wearer.GetStat(id);
        json entry = json::object();
        entry["id"] = id;
        entry["value"] = isSigned ? static_cast<int64_t>(raw) : static_cast<int64_t>(static_cast<uint32_t>(raw));
        stats.push_back(std::move(entry));
    }
    return stats;
}

namespace {

void AddWearerFields(json& out, const game::Unit& wearer) {
    out["flagsEx"] = wearer.FlagsEx();
    out["name"] = wearer.Name();

    // A skill level is the one thing the stat lists can't carry. `level` is the bonused
    // value the tooltip engine wants; `hard` is the invested points, so a consumer can
    // recover the gear bonus as level - hard.
    auto skills = wearer.GetAllSkills();
    std::ranges::sort(skills, {}, &game::Unit::SkillInfo::skillId);
    json skillsJson = json::array();
    for (const auto& skill : skills) {
        json entry = json::object();
        entry["skill"] = skill.skillId;
        entry["hard"] = skill.baseLevel;
        entry["level"] = skill.totalLevel;
        skillsJson.push_back(std::move(entry));
    }
    out["skills"] = std::move(skillsJson);
}

}  // namespace

json UnitToJson(const game::Unit& unit, Detail detail) {
    json out = json::object();
    if (!unit) {
        return out;
    }

    const auto type = unit.Type();
    out["unitType"] = type;
    out["classId"] = unit.ClassId();

    switch (type) {
        case game::UnitType::Item:
            AddItemFields(out, unit, detail);
            break;
        case game::UnitType::Player:
            AddWearerFields(out, unit);
            out["area"] = unit.Area();
            // Reads a client global, so it only answers for the local player.
            out["hand"] = unit.WeaponSwitch();
            break;
        case game::UnitType::Monster:
            AddWearerFields(out, unit);
            break;
        default:
            break;
    }
    return out;
}

}  // namespace d2bs::js::characterstate
