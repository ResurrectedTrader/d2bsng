#include "components/characterstate/CharacterState.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "components/config/AppConfig.h"
#include "game/Console.h"
#include "game/Constants.h"
#include "game/Finders.h"
#include "game/GameHelpers.h"
#include "game/Types.h"
#include "game/Unit.h"

namespace d2bs::framework::characterstate {

namespace game = d2bs::game;
namespace config = d2bs::config;

namespace {

// NOLINTNEXTLINE(readability-identifier-naming) - 'json' is nlohmann's conventional alias spelling
using json = nlohmann::json;

// schemaVersion of the wire contract (owned by the manager plan).
constexpr int32_t SCHEMA_VERSION = 1;

// D2 item flag (D2Structs ITEM_FLAG_ETHEREAL).
constexpr uint32_t ITEM_FLAG_ETHEREAL = 0x00400000;

// item_numsockets stat (ItemStatCost.txt): an item's total socket capacity.
constexpr uint32_t STAT_NUMSOCKETS = 194;

// Debounce window: sample the state at most this often, and only send once it has
// stopped changing across a sample, so a burst of changes coalesces into one send.
constexpr auto CHECK_INTERVAL = std::chrono::seconds{1};

// Curated stat ids the manager's StatsPanel labels (D2BotNG stats.ts). Order is
// irrelevant; the manager renders only ids it recognises.
constexpr std::array<uint32_t, 22> STAT_IDS = {0,  1,  2,  3,  7,  9,  12, 13, 14, 15, 39,
                                               40, 41, 42, 43, 44, 45, 46, 80, 96, 99, 105};

// Container bucket indices. Two slot-based containers (equipped, merc) carry the
// equip-location in each item's `x` (y = 0); the rest are grids, except stash
// which is sent as pages.
constexpr size_t BUCKET_EQUIPPED = 0;
constexpr size_t BUCKET_MERC = 1;
constexpr size_t BUCKET_INVENTORY = 2;
constexpr size_t BUCKET_CUBE = 3;
constexpr size_t BUCKET_BELT = 4;
constexpr size_t BUCKET_STASH = 5;
constexpr size_t BUCKET_COUNT = 6;
static_assert(BUCKET_COUNT == 6);

// Quest / waypoint bounds (active difficulty only; identity carries which). A quest
// is complete when its reward is granted or pending - not the COMPLETEDNOW/BEFORE
// bits, which the record load clears, so a quest done in a prior game reads as
// incomplete.
constexpr uint32_t QUEST_COUNT = 41;
constexpr uint32_t QFLAG_REWARDGRANTED = 0;
constexpr uint32_t QFLAG_REWARDPENDING = 1;
constexpr uint32_t WAYPOINT_COUNT = 39;

// Cheap per-item fields, sampled up front. The Unit handle is kept so the costly
// reads (name/description/sockets) run only when a container is actually rebuilt.
struct ItemRec {
    game::Unit unit;
    uint32_t gid = 0;
    game::ItemLocation loc = game::ItemLocation::Null;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    std::string code;
    int32_t quality = 0;
    uint32_t flags = 0;
};

ItemRec MakeItemRec(const game::Unit& item, game::ItemLocation loc) {
    const auto pos = item.Pos();
    const auto size = item.Size();
    return ItemRec{.unit = item,
                   .gid = item.Id(),
                   .loc = loc,
                   .x = pos.x,
                   .y = pos.y,
                   .w = size.width,
                   .h = size.height,
                   .code = item.ItemCode(),
                   .quality = static_cast<int32_t>(item.Quality()),
                   .flags = item.ItemFlags()};
}

// Cheap, Description()-free fingerprint of a container's contents. Includes the
// full flag bitmask so identify/socket/ethereal changes (which don't move the
// item) still re-send.
std::string StructuralKey(const std::vector<ItemRec>& items) {
    std::string key;
    for (const auto& it : items) {
        key += fmt::format("{}:{},{},{}x{},{},q{},f{};", it.gid, it.x, it.y, it.w, it.h, it.code, it.quality, it.flags);
    }
    return key;
}

// Read a string cell from a game .txt table - the game-layer backing of the
// getBaseStat JS global kolbot uses. Empty if the cell is missing / not a string.
std::string GetTxtString(std::string_view table, uint32_t row, std::string_view column) {
    const auto value = game::GetTxtValue(table, row, column);
    const auto* s = std::get_if<std::string>(&value);
    return s != nullptr ? *s : std::string{};
}

// Numeric cell from a game .txt table; fallback when the cell is missing / not numeric.
int64_t GetTxtInt(std::string_view table, uint32_t row, std::string_view column, int64_t fallback) {
    const auto value = game::GetTxtValue(table, row, column);
    const auto* n = std::get_if<int64_t>(&value);
    return n != nullptr ? *n : fallback;
}

// Inventory-graphic name the manager fetches as "<image>.dc6"; port of kolbot's
// Item.getItemCode. The raw code is wrong for most items: set/unique use the
// items.txt setinvfile / uniqueitems.txt invfile graphic, exc/elite collapse to
// their normcode (shared graphic), and ring/amulet/jewel/charm append a 1-based
// gfx variant. (setinvfile is exactly kolbot's hardcoded set-classid switch.)
std::string ResolveImageCode(const game::Unit& item) {
    const std::string code = item.ItemCode();
    const uint32_t classId = item.ClassId();

    std::string image;
    const auto quality = item.Quality();
    if (quality == game::ItemQuality::Set) {
        image = GetTxtString("items", classId, "setinvfile");
    } else if (quality == game::ItemQuality::Unique) {
        if (const auto uniqueId = item.UniqueId()) {
            image = GetTxtString("uniqueitems", *uniqueId, "invfile");
        }
    }
    if (!image.empty()) {
        return image;
    }

    // Default path: Tiara/Diadem keep their own code; everything else collapses to
    // its normal-tier code (normcode) so exc/elite share the base graphic.
    if (code == "ci2" || code == "ci3") {
        image = code;
    } else {
        image = GetTxtString("items", classId, "normcode");
        if (image.empty()) {
            image = code;
        }
    }
    std::erase(image, ' ');

    // Ring/amulet/jewel/charm carry multiple random inventory graphics; append the
    // item's variant index (1-based) so e.g. a ring resolves to rin1..rin5.
    if (GetTxtInt("itemtypes", item.ItemType(), "varinvgfx", 0) > 0) {
        image += std::to_string(item.GfxIndex() + 1);
    }
    return image;
}

// D2 ITEM_FLAG_IDENTIFIED bit (reference Constants.h ITEM_FLAG_IDENTIFIED).
constexpr uint32_t ITEM_FLAG_IDENTIFIED = 0x00000010;

// Palette-shift values above white (20) aren't real inventory colors -> treat as none.
constexpr int64_t MAX_PALETTE_INDEX = 20;

// Magic/rare color: the matching affix's transformcolor (magicprefix/magicsuffix),
// suffix then prefix, first with a real color (-1 = none). The "affixes" txt table
// is the game's combined affix array, registered 1-based, so the affix id indexes
// it directly. Mirrors the game's ITEMS_GetColor (kolbot's old map hand-copied this).
int32_t MatchAffixColor(const game::Unit& item) {
    const auto affixColor = [](uint16_t affixId) -> int32_t {
        if (affixId == 0) {
            return -1;
        }
        const int64_t tc = GetTxtInt("affixes", affixId, "transformcolor", -1);
        return (tc < 0 || tc > MAX_PALETTE_INDEX) ? -1 : static_cast<int32_t>(tc);
    };
    for (const uint16_t suffix : item.SuffixNums()) {
        if (const int32_t c = affixColor(suffix); c >= 0) {
            return c;
        }
    }
    for (const uint16_t prefix : item.PrefixNums()) {
        if (const int32_t c = affixColor(prefix); c >= 0) {
            return c;
        }
    }
    return -1;
}

// Inventory palette-shift index (-1 = none): the magic/rare affix transformcolor or the
// unique/set invtransform, per the game's ITEMS_GetColor. Whether it actually paints is
// gated by the base item's InvTrans (sent as invTrans, gated manager-side), so this no
// longer replicates kolbot's item-type allowlist.
int32_t ItemColor(const game::Unit& item) {
    const auto quality = item.Quality();
    switch (quality) {
        case game::ItemQuality::Magic:
        case game::ItemQuality::Set:
        case game::ItemQuality::Rare:
        case game::ItemQuality::Unique:
            break;
        default:
            return -1;
    }

    if (quality == game::ItemQuality::Magic || quality == game::ItemQuality::Rare) {
        return MatchAffixColor(item);
    }

    if (quality == game::ItemQuality::Unique) {
        if (const auto uniqueId = item.UniqueId()) {
            const int64_t shift = GetTxtInt("uniqueitems", *uniqueId, "invtransform", -1);
            return (shift < 0 || shift > MAX_PALETTE_INDEX) ? -1 : static_cast<int32_t>(shift);
        }
        return -1;
    }

    // Set: an unidentified set item doesn't carry its specific set-item id on the
    // client (dwFileIndex is unset until ID), so it can't be looked up - fall back to
    // a generic colour. Once identified, read invtransform by id, mirroring the unique
    // branch and the game's ITEMS_GetColor (setitems[dwFileIndex].nInvTransform).
    if ((item.ItemFlags() & ITEM_FLAG_IDENTIFIED) == 0) {
        return 13;  // lightyellow (unidentified set)
    }
    if (const auto setId = item.UniqueId()) {
        const int64_t shift = GetTxtInt("setitems", *setId, "invtransform", -1);
        return (shift < 0 || shift > MAX_PALETTE_INDEX) ? -1 : static_cast<int32_t>(shift);
    }
    return -1;
}

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

// Socket list: one entry per socket (total = numsockets stat) - the socketed code
// (jewels get a gfx variant) or "gemsocket" when empty, so empty sockets show. Port
// of kolbot's Item.getItemSockets, including its 2-wide reorder; other sizes keep order.
std::vector<std::string> BuildSockets(const game::Unit& item, uint32_t sizeX, uint32_t sizeY) {
    const int32_t sockets = item.GetStat(STAT_NUMSOCKETS);
    if (sockets <= 0) {
        return {};
    }

    std::vector<game::Unit> filled;
    for (const auto& socket : item.GetItems()) {
        filled.push_back(socket);
    }

    // Render position -> filled-socket index. kolbot reorders 2-wide items to the
    // visual socket layout; everything else stays in storage order.
    std::vector<int32_t> order;
    if (sizeX == 2 && sizeY == 3) {
        if (sockets == 4) {
            order = {0, 3, 2, 1};
        } else if (sockets == 5) {
            order = {1, 4, 0, 3, 2};
        } else if (sockets == 6) {
            order = {0, 3, 1, 4, 2, 5};
        }
    } else if (sizeX == 2 && sizeY == 4) {
        if (sockets == 5) {
            order = {1, 4, 0, 3, 2};
        } else if (sockets == 6) {
            order = {0, 3, 1, 4, 2, 5};
        }
    }
    if (order.empty()) {
        order.resize(static_cast<size_t>(sockets));
        for (int32_t i = 0; i < sockets; ++i) {
            order[static_cast<size_t>(i)] = i;
        }
    }

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(sockets));
    for (const int32_t idx : order) {
        if (idx >= 0 && static_cast<size_t>(idx) < filled.size()) {
            const auto& socket = filled[static_cast<size_t>(idx)];
            std::string code = socket.ItemCode();
            if (GetTxtInt("itemtypes", socket.ItemType(), "varinvgfx", 0) > 0) {
                code += std::to_string(socket.GfxIndex() + 1);
            }
            out.push_back(std::move(code));
        } else {
            out.emplace_back("gemsocket");
        }
    }
    return out;
}

json BuildItem(const ItemRec& rec, const std::string& header) {
    json item = json::object();
    // Inventory-graphic name, not the raw item code (see ResolveImageCode).
    item["image"] = ResolveImageCode(rec.unit);
    item["title"] = rec.unit.Name();
    // Game tooltip, lines reversed to display order (see ReverseLines).
    item["description"] = ReverseLines(rec.unit.Description());
    item["itemColor"] = ItemColor(rec.unit);
    // Base item's palette-transform group (InvTrans); the manager selects which color
    // table to apply by this and gates groups the game won't tint. See manager rendering.
    item["invTrans"] = static_cast<int32_t>(GetTxtInt("items", rec.unit.ClassId(), "InvTrans", 0));
    item["textColor"] = -1;
    item["header"] = header;

    json sockets = json::array();
    for (const auto& code : BuildSockets(rec.unit, rec.w, rec.h)) {
        sockets.push_back(code);
    }
    item["sockets"] = std::move(sockets);

    item["gid"] = rec.gid;
    item["ethereal"] = (rec.flags & ITEM_FLAG_ETHEREAL) != 0U;
    item["quality"] = rec.quality;
    item["location"] = static_cast<int32_t>(rec.loc);
    // Slot containers (equipped/merc) carry the equip-location in x with y = 0;
    // grid containers carry the cell. Both come straight from Unit::Pos().
    item["x"] = rec.x;
    item["y"] = rec.y;
    item["w"] = rec.w;
    item["h"] = rec.h;
    return item;
}

json BuildContainer(size_t bucket, const std::vector<ItemRec>& items, game::Size dims, const std::string& header) {
    json itemsArr = json::array();
    for (const auto& rec : items) {
        itemsArr.push_back(BuildItem(rec, header));
    }

    json container = json::object();
    if (bucket == BUCKET_EQUIPPED || bucket == BUCKET_MERC) {
        // Slot container: no grid dimensions.
        container["items"] = std::move(itemsArr);
    } else if (bucket == BUCKET_STASH) {
        json page = json::object();
        page["index"] = 0;
        page["name"] = "Personal";
        page["width"] = dims.width;
        page["height"] = dims.height;
        page["items"] = std::move(itemsArr);
        json pages = json::array();
        pages.push_back(std::move(page));
        container["pages"] = std::move(pages);
    } else {
        container["width"] = dims.width;
        container["height"] = dims.height;
        container["items"] = std::move(itemsArr);
    }
    return container;
}

json BuildIdentity(const game::Unit& player) {
    json identity = json::object();
    identity["account"] = game::GetAccountName();
    identity["realm"] = game::GetRealmShort();
    identity["charName"] = game::GetPlayerName();
    identity["charClass"] = static_cast<int32_t>(player.ClassId());
    identity["level"] = player.CharLevel();
    identity["area"] = player.Area();
    identity["difficulty"] = static_cast<int32_t>(game::GetDifficulty());
    // hardcore/expansion are derivable from the top-level charFlags; ladder is a
    // separate BnetData flag, so it stays here.
    identity["ladder"] = game::IsLadder().value_or(0) != 0;
    return identity;
}

json BuildStats(const game::Unit& player) {
    json stats = json::array();
    for (const uint32_t id : STAT_IDS) {
        const int32_t raw = player.GetStat(id);
        // itemstatcost flags genuinely-signed stats (resists, etc.) as Signed=1 -> sign-extend;
        // the rest are unsigned -> zero-extend, so 32-bit experience doesn't read negative.
        const bool unsignedStat = GetTxtInt("itemstatcost", id, "signed", 0) != 1;
        const int64_t value =
            unsignedStat ? static_cast<int64_t>(static_cast<uint32_t>(raw)) : static_cast<int64_t>(raw);
        json entry = json::object();
        entry["id"] = id;
        entry["value"] = value;
        stats.push_back(std::move(entry));
    }
    return stats;
}

// Combined progression-derived block, kept together for a single fingerprint: character
// flags, per-skill hard/soft points, and the active difficulty's completed quests +
// waypoints. The snapshot splits it on the wire - charFlags and skills go to the top
// level, progression carries only quests + waypoints (identity.difficulty tags which).
json BuildProgression(const game::Unit& player) {
    json progression = json::object();
    progression["charFlags"] = game::GetCharFlags();

    json skills = json::array();
    for (const auto& skill : player.GetAllSkills()) {
        if (skill.baseLevel == 0) {
            continue;  // only skills with hard points invested
        }
        // hard = points manually invested; soft = bonus from +skills gear/charms.
        const uint32_t soft = skill.totalLevel >= skill.baseLevel ? skill.totalLevel - skill.baseLevel : 0U;
        json entry = json::object();
        entry["id"] = skill.skillId;
        entry["hard"] = skill.baseLevel;
        entry["soft"] = soft;
        skills.push_back(std::move(entry));
    }
    progression["skills"] = std::move(skills);

    json quests = json::array();
    for (uint32_t questId = 0; questId < QUEST_COUNT; ++questId) {
        if (game::GetQuestFlag(questId, QFLAG_REWARDGRANTED) != 0 ||
            game::GetQuestFlag(questId, QFLAG_REWARDPENDING) != 0) {
            quests.push_back(questId);  // reward granted or pending == completed
        }
    }
    progression["quests"] = std::move(quests);

    json waypoints = json::array();
    for (uint32_t wp = 0; wp < WAYPOINT_COUNT; ++wp) {
        if (game::HasWaypoint(wp)) {
            waypoints.push_back(wp);
        }
    }
    progression["waypoints"] = std::move(waypoints);

    return progression;
}

// Add `container` to the message on a keyframe or when its contents changed,
// updating the stored fingerprint.
void EmitContainer(json& containers, std::string_view name, size_t bucket, const std::vector<ItemRec>& items,
                   game::Size dims, bool keyframe, size_t hash, std::optional<size_t>& fingerprint, bool& anyChange,
                   const std::string& header) {
    const bool changed = !fingerprint.has_value() || *fingerprint != hash;
    // Keyframes carry every container (including empty) so the client has the full
    // set and grid sizes; steady state re-sends one only when its contents change.
    if (keyframe) {
        fingerprint = hash;
    } else if (changed) {
        fingerprint = hash;
    } else {
        return;
    }
    containers[std::string(name)] = BuildContainer(bucket, items, dims, header);
    anyChange = true;
}

// Compact kill-counter block (a delta since the last send - see the OnTick emit).
// byClass entries are {id, spec, count} bucketed by (class id, SpecType), so each
// rarity of a class is counted separately - spec is the same bitfield as JS
// unit.spectype (0x02 champion, 0x04 unique pack, 0x08 minion, 0 normal).
// bySuperUnique entries are {id, count} keyed by SuperUniques.txt index. Same flat
// array-of-objects shape as stats/skills, ids numeric. The two buckets are disjoint
// (a super-unique is counted only under bySuperUnique, never under byClass), and the
// manager just adds the deltas to its own tally. std::map iteration is key-ordered,
// so the output is sorted (byClass by class then spec) and deterministic.
json BuildKills(const std::map<std::pair<uint32_t, uint32_t>, uint32_t>& byClass,
                const std::map<uint32_t, uint32_t>& bySuperUnique) {
    json byClassArr = json::array();
    for (const auto& [classSpec, count] : byClass) {
        json entry = json::object();
        entry["id"] = classSpec.first;
        entry["spec"] = classSpec.second;
        entry["count"] = count;
        byClassArr.push_back(std::move(entry));
    }
    json bySuperUniqueArr = json::array();
    for (const auto& [id, count] : bySuperUnique) {
        json entry = json::object();
        entry["id"] = id;
        entry["count"] = count;
        bySuperUniqueArr.push_back(std::move(entry));
    }
    json kills = json::object();
    kills["byClass"] = std::move(byClassArr);
    kills["bySuperUnique"] = std::move(bySuperUniqueArr);
    return kills;
}

}  // namespace

CharacterState& CharacterState::Instance() {
    static CharacterState instance;
    return instance;
}

void CharacterState::OnTick(game::GameState state, bool sessionEntered) {
    if (state != game::GameState::InGame) {
        wasInGame_ = false;
        return;
    }

    auto player = game::Unit::Player();
    if (!player) {
        return;
    }

    // No manager target yet (no "Handle" WM_COPYDATA seen) -> nowhere to send.
    // Bail before mutating any state so the first tick after the handle arrives
    // still produces a keyframe.
    const auto managerHandle = config::GetAppConfig().managerHandle.load(std::memory_order_relaxed);
    if (managerHandle == 0) {
        return;
    }

    // A new game (forces a keyframe) is the Menu/Null->InGame transition, the
    // first in-game tick, or a changed game name.
    const std::string gameName = game::GetGameName();
    const bool keyframe = sessionEntered || !wasInGame_ || gameName != lastGameName_;
    wasInGame_ = true;

    // system_clock (wall clock) so the same value doubles as the epoch-ms
    // updatedAt below; the sampling cadence doesn't need a monotonic clock.
    const auto now = std::chrono::system_clock::now();
    if (!keyframe && lastCheck_.has_value() && (now - *lastCheck_) < CHECK_INTERVAL) {
        return;
    }
    lastCheck_ = now;

    if (keyframe) {
        lastGameName_ = gameName;
        gameId_ = fmt::format("{}#{}", gameName, ++createCounter_);
        identityFingerprint_.reset();
        statsFingerprint_.reset();
        progressionFingerprint_.reset();
        for (auto& fingerprint : containerFingerprints_) {
            fingerprint.reset();
        }
    }

    // Bucket every owned item once (cheap reads only); name/description/sockets
    // are deferred to BuildContainer for the containers that actually changed.
    std::vector<ItemRec> equipped;
    std::vector<ItemRec> merc;
    std::vector<ItemRec> inventory;
    std::vector<ItemRec> cube;
    std::vector<ItemRec> belt;
    std::vector<ItemRec> stash;
    for (const auto& item : player.GetItems()) {
        const auto loc = item.ItemLocation();
        std::vector<ItemRec>* bucket = nullptr;
        switch (loc) {
            case game::ItemLocation::Equip:
                bucket = &equipped;
                break;
            case game::ItemLocation::Inventory:
                bucket = &inventory;
                break;
            case game::ItemLocation::Cube:
                bucket = &cube;
                break;
            case game::ItemLocation::Belt:
                bucket = &belt;
                break;
            case game::ItemLocation::Stash:
                bucket = &stash;
                break;
            default:
                continue;
        }
        bucket->push_back(MakeItemRec(item, loc));
    }

    // Merc gear (slot container). FindMerc walks the monster table; cheap at the
    // ~1s cadence. Merc carries only equipped items.
    if (auto m = player.FindMerc()) {
        for (const auto& item : m->GetItems()) {
            if (item.ItemLocation() == game::ItemLocation::Equip) {
                merc.push_back(MakeItemRec(item, game::ItemLocation::Equip));
            }
        }
    }

    // Build every section fingerprint first, so the debounce can tell whether the
    // state is still changing before we commit to diffing or sending anything.
    json identity = BuildIdentity(player);
    const size_t identityHash = std::hash<std::string>{}(identity.dump());
    json stats = BuildStats(player);
    const size_t statsHash = std::hash<std::string>{}(stats.dump());
    json progression = BuildProgression(player);
    const size_t progressionHash = std::hash<std::string>{}(progression.dump());
    const std::array<size_t, CONTAINER_COUNT> containerHashes = {
        std::hash<std::string>{}(StructuralKey(equipped)),  std::hash<std::string>{}(StructuralKey(merc)),
        std::hash<std::string>{}(StructuralKey(inventory)), std::hash<std::string>{}(StructuralKey(cube)),
        std::hash<std::string>{}(StructuralKey(belt)),      std::hash<std::string>{}(StructuralKey(stash))};

    // Debounce: combine the slow-moving section fingerprints into one signature.
    // While it differs from the previous sample the state is still settling, so
    // remember it and wait; only once it stops changing do we diff + send. Stats
    // and kills are deliberately left out: experience/gold/kills tick continuously
    // while farming and would never let the signature settle, starving every send.
    // Each instead rides its own fingerprint below and flows at the ~1s cadence.
    // Keyframes bypass the wait entirely.
    std::string signature = fmt::format("{}:{}", identityHash, progressionHash);
    for (const size_t containerHash : containerHashes) {
        signature += fmt::format(":{}", containerHash);
    }
    const size_t combined = std::hash<std::string>{}(signature);
    if (!keyframe && (!pendingHash_.has_value() || *pendingHash_ != combined)) {
        pendingHash_ = combined;
        return;
    }
    pendingHash_ = combined;

    json snapshot = json::object();
    snapshot["schemaVersion"] = SCHEMA_VERSION;
    snapshot["gameId"] = gameId_;
    snapshot["keyframe"] = keyframe;
    // Wall-clock time the snapshot was assembled (Unix epoch ms) so the manager
    // can show "last updated" from the game side rather than its receive time.
    snapshot["updatedAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    // The player's currently active weapon set (0/1); a snapshot-root field, not
    // per-item. WeaponSwitch reads a player global, so it comes off the player unit.
    snapshot["hand"] = player.WeaponSwitch();
    bool anyChange = keyframe;

    if (keyframe || !identityFingerprint_.has_value() || *identityFingerprint_ != identityHash) {
        snapshot["identity"] = std::move(identity);
        identityFingerprint_ = identityHash;
        anyChange = true;
    }
    if (keyframe || !statsFingerprint_.has_value() || *statsFingerprint_ != statsHash) {
        snapshot["stats"] = std::move(stats);
        statsFingerprint_ = statsHash;
        anyChange = true;
    }
    if (keyframe || !progressionFingerprint_.has_value() || *progressionFingerprint_ != progressionHash) {
        // charFlags and skills ride at the snapshot root; progression on the wire carries
        // only the current difficulty's quests + waypoints (identity.difficulty tags it).
        snapshot["charFlags"] = std::move(progression["charFlags"]);
        snapshot["skills"] = std::move(progression["skills"]);
        json prog = json::object();
        prog["quests"] = std::move(progression["quests"]);
        prog["waypoints"] = std::move(progression["waypoints"]);
        snapshot["progression"] = std::move(prog);
        progressionFingerprint_ = progressionHash;
        anyChange = true;
    }

    const std::string header = fmt::format("{} / {}", game::GetAccountName(), game::GetPlayerName());
    json containers = json::object();
    EmitContainer(containers, "equipped", BUCKET_EQUIPPED, equipped, {.width = 0, .height = 0}, keyframe,
                  containerHashes[BUCKET_EQUIPPED], containerFingerprints_[BUCKET_EQUIPPED], anyChange, header);
    EmitContainer(containers, "merc", BUCKET_MERC, merc, {.width = 0, .height = 0}, keyframe,
                  containerHashes[BUCKET_MERC], containerFingerprints_[BUCKET_MERC], anyChange, header);
    EmitContainer(containers, "inventory", BUCKET_INVENTORY, inventory, {.width = 10, .height = 4}, keyframe,
                  containerHashes[BUCKET_INVENTORY], containerFingerprints_[BUCKET_INVENTORY], anyChange, header);
    EmitContainer(containers, "cube", BUCKET_CUBE, cube, {.width = 3, .height = 4}, keyframe,
                  containerHashes[BUCKET_CUBE], containerFingerprints_[BUCKET_CUBE], anyChange, header);
    EmitContainer(containers, "belt", BUCKET_BELT, belt, {.width = 4, .height = 4}, keyframe,
                  containerHashes[BUCKET_BELT], containerFingerprints_[BUCKET_BELT], anyChange, header);
    EmitContainer(containers, "stash", BUCKET_STASH, stash, {.width = 6, .height = 8}, keyframe,
                  containerHashes[BUCKET_STASH], containerFingerprints_[BUCKET_STASH], anyChange, header);
    if (!containers.empty()) {
        snapshot["containers"] = std::move(containers);
    }

    // Kills are sent as a delta since the last send, not a running total: the maps
    // accumulate what the death hook observed and are cleared once emitted, so the
    // manager just adds each delta to its own persistent tally - no per-game reset
    // or gameId bookkeeping on its side. They sit outside the debounce signature so
    // a kill never gates the other sections, but still inherit the ~1s cadence and
    // settle wait, coalescing into a larger delta while the rest settles. (A delta
    // isn't idempotent - clearing on emit trusts the synchronous WM_COPYDATA send.)
    if (!killsByClass_.empty() || !killsBySuperUnique_.empty()) {
        snapshot["kills"] = BuildKills(killsByClass_, killsBySuperUnique_);
        killsByClass_.clear();
        killsBySuperUnique_.clear();
        anyChange = true;
    }

    if (!anyChange) {
        return;
    }

    json envelope = json::object();
    envelope["profile"] = config::GetAppConfig().GetProfileName();
    envelope["func"] = "characterState";
    envelope["args"] = json::array({snapshot.dump(-1, ' ', false, json::error_handler_t::replace)});

    game::SendIPC(0, envelope.dump(-1, ' ', false, json::error_handler_t::replace), managerHandle);
}

void CharacterState::RecordKill(uint32_t unitId) {
    // Game thread (death packet hook): the per-resolve GameReadLock no-ops because
    // the frame write lock is held (see the class doc + game/GameLock.h), so this
    // resolve is lock-free and consistent.
    const auto monster = game::Unit::Find(unitId, game::UnitType::Monster);
    if (!monster) {
        return;  // already despawned, or not a resolvable monster
    }
    // Each kill lands in exactly one bucket - no double counting. A super-unique
    // (UniqueId set: Pindleskin, Eldritch, ...) is tracked only by its
    // SuperUniques.txt index; everything else (trash, champions, random uniques,
    // act bosses) by {class id, SpecType} so its rarity is preserved. The maps hold
    // the unsent delta - OnTick emits and clears them.
    if (const auto superUnique = monster->UniqueId()) {
        ++killsBySuperUnique_[*superUnique];
    } else {
        ++killsByClass_[{monster->ClassId(), monster->SpecType()}];
    }
}

}  // namespace d2bs::framework::characterstate
