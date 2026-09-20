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

void VisitStatLists(UnitVisitor& visitor, const game::Unit& item) {
    visitor.BeginArray("statsLists");
    for (const auto& list : item.GetStatLists()) {
        visitor.BeginElement();
        visitor.Int("stateNo", list.stateNo);
        visitor.Int("flags", list.flags);
        visitor.BeginArray("stats");
        for (const auto& stat : list.stats) {
            visitor.BeginElement();
            visitor.Int("id", stat.statId);
            visitor.Int("value", RawValue(stat));
            if (stat.subIndex != 0) {
                visitor.Int("layer", stat.subIndex);
            }
            visitor.EndElement();
        }
        visitor.EndArray();
        visitor.EndElement();
    }
    visitor.EndArray();
}

void VisitItem(UnitVisitor& visitor, const game::Unit& item, Detail detail) {
    // The compiled code is space padded, not NUL padded.
    std::string code = item.ItemCode();
    while (!code.empty() && code.back() == ' ') {
        code.pop_back();
    }
    visitor.Str("code", code);

    visitor.Int("quality", static_cast<int64_t>(item.Quality()));
    visitor.Int("itemFlags", item.ItemFlags());
    visitor.Int("format", item.ItemFormat());
    const auto fileIndex = item.FileIndex();
    visitor.Int("fileIndex", fileIndex.has_value() ? static_cast<int64_t>(*fileIndex) : -1);
    visitor.Int("rarePrefix", item.RarePrefixNum());
    visitor.Int("rareSuffix", item.RareSuffixNum());
    visitor.Int("autoAffix", item.AutoAffixNum());
    const auto prefixes = item.PrefixNums();
    visitor.Ints("magicPrefix", prefixes);
    const auto suffixes = item.SuffixNums();
    visitor.Ints("magicSuffix", suffixes);
    visitor.Int("itemLevel", item.ItemLevel());
    visitor.Int("earLevel", item.EarLevel());
    visitor.Str("playerName", item.ItemPlayerName());
    // Which of the random inventory graphics this instance rolled, for item types with
    // varinvgfx (rings, amulets, jewels, charms). Nothing else in the document implies
    // it, so a consumer resolving the graphic itself needs it.
    visitor.Int("gfxIndex", item.GfxIndex());

    if (detail == Detail::Full) {
        visitor.Str("title", item.Name());
        // Game tooltip, lines reversed to display order (see ReverseLines).
        visitor.Str("description", ReverseLines(item.Description()));
        VisitStatLists(visitor, item);
    }

    visitor.Int("gid", item.Id());
    visitor.Int("location", static_cast<int64_t>(item.ItemLocation()));
    // Slot containers (equipped/merc) carry the equip-location in x with y = 0;
    // grid containers carry the cell.
    const auto pos = item.Pos();
    const auto size = item.Size();
    visitor.Int("x", pos.x);
    visitor.Int("y", pos.y);
    visitor.Int("w", size.width);
    visitor.Int("h", size.height);

    // An item's contained units are its socket fillers, and the inventory chain is
    // append-ordered, so chain order is socket order. Fillers are contiguous from 0, so
    // array position is the socket index; the total is stat 194.
    const auto fillers = item.GetItems();
    if (!fillers.empty()) {
        visitor.BeginArray("sockets");
        for (const auto& filler : fillers) {
            visitor.BeginElement();
            VisitUnit(visitor, filler, detail);
            visitor.EndElement();
        }
        visitor.EndArray();
    }
}

void VisitWearer(UnitVisitor& visitor, const game::Unit& wearer) {
    visitor.Int("flagsEx", wearer.FlagsEx());
    visitor.Str("name", wearer.Name());

    // A skill level is the one thing the stat lists can't carry. `level` is the bonused
    // value the tooltip engine wants; `hard` is the invested points, so a consumer can
    // recover the gear bonus as level - hard.
    auto skills = wearer.GetAllSkills();
    std::ranges::sort(skills, {}, &game::Unit::SkillInfo::skillId);
    visitor.BeginArray("skills");
    for (const auto& skill : skills) {
        visitor.BeginElement();
        visitor.Int("skill", skill.skillId);
        visitor.Int("hard", skill.baseLevel);
        visitor.Int("level", skill.totalLevel);
        visitor.EndElement();
    }
    visitor.EndArray();
}

// Builds the nlohmann json document from the visit events, tracking the current insertion
// container on a stack: the top is an object for Int/Str/Ints, an array for BeginElement.
// json object nodes stay put across sibling inserts (std::map), and an array's elements are
// only pushed while no child of a prior element is still on the stack, so held pointers
// never dangle.
class JsonVisitor final : public UnitVisitor {
   public:
    JsonVisitor() { stack_.push_back(&root_); }

    void Int(std::string_view key, int64_t value) override { (*stack_.back())[std::string(key)] = value; }
    void Str(std::string_view key, std::string_view value) override { (*stack_.back())[std::string(key)] = value; }
    void Ints(std::string_view key, std::span<const uint16_t> values) override {
        json arr = json::array();
        for (const auto value : values) {
            arr.push_back(value);
        }
        (*stack_.back())[std::string(key)] = std::move(arr);
    }
    void BeginArray(std::string_view key) override {
        json& arr = ((*stack_.back())[std::string(key)] = json::array());
        stack_.push_back(&arr);
    }
    void BeginElement() override {
        json& arr = *stack_.back();
        arr.push_back(json::object());
        stack_.push_back(&arr.back());
    }
    void EndElement() override { stack_.pop_back(); }
    void EndArray() override { stack_.pop_back(); }

    json Take() { return std::move(root_); }

   private:
    json root_ = json::object();
    std::vector<json*> stack_;
};

// Curated stat ids the manager's StatsPanel labels (D2BotNG stats.ts). Order is
// irrelevant; the manager renders only ids it recognises.
constexpr std::array<uint32_t, 22> STAT_IDS = {0,  1,  2,  3,  7,  9,  12, 13, 14, 15, 39,
                                               40, 41, 42, 43, 44, 45, 46, 80, 96, 99, 105};

}  // namespace

void VisitUnit(UnitVisitor& visitor, const game::Unit& unit, Detail detail) {
    if (!unit) {
        return;
    }

    const auto type = unit.Type();
    visitor.Int("unitType", static_cast<int64_t>(type));
    visitor.Int("classId", unit.ClassId());

    switch (type) {
        case game::UnitType::Item:
            VisitItem(visitor, unit, detail);
            break;
        case game::UnitType::Player:
            VisitWearer(visitor, unit);
            visitor.Int("area", unit.Area());
            // Reads a client global, so it only answers for the local player.
            visitor.Int("hand", unit.WeaponSwitch());
            break;
        case game::UnitType::Monster:
            VisitWearer(visitor, unit);
            break;
        default:
            break;
    }
}

json UnitToJson(const game::Unit& unit, Detail detail) {
    JsonVisitor visitor;
    VisitUnit(visitor, unit, detail);
    return visitor.Take();
}

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

}  // namespace d2bs::js::characterstate
