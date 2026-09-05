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
#include <vector>

#include <fmt/format.h>

#include "components/characterstate/UnitJson.h"
#include "config/AppConfig.h"
#include "game/Finders.h"
#include "game/GameHelpers.h"
#include "game/Types.h"
#include "game/Unit.h"

namespace d2bs::js::characterstate {

namespace {

// NOLINTNEXTLINE(readability-identifier-naming) - 'json' is nlohmann's conventional alias spelling
using json = nlohmann::json;

// schemaVersion of the wire contract (owned by the manager plan).
constexpr int32_t SCHEMA_VERSION = 2;

// Debounce window: sample the state at most this often, and only send once it has
// stopped changing across a sample, so a burst of changes coalesces into one send.
constexpr auto CHECK_INTERVAL = std::chrono::seconds{1};

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

// Quest / waypoint bounds (active difficulty only; identity carries which). A quest
// is complete when its reward is granted or pending - not the COMPLETEDNOW/BEFORE
// bits, which the record load clears, so a quest done in a prior game reads as
// incomplete.
constexpr uint32_t QUEST_COUNT = 41;
constexpr uint32_t QFLAG_REWARDGRANTED = 0;
constexpr uint32_t QFLAG_REWARDPENDING = 1;
constexpr uint32_t WAYPOINT_COUNT = 39;

// Cells of a grid container, straight from the layout the game draws the panel from - never
// a constant, because a mod resizes a panel by rewriting the inventory.txt row that layout is
// built from (PlugY's ActiveBigStash makes the stash 10x10) and items then land outside the
// vanilla bounds.
//
// Zero when the game has not populated the record yet, which reads the same as the non-grid
// containers: unknown, so draw no grid. Deliberately not a vanilla fallback, which would assert
// a size we know can be wrong, and deliberately not grown to fit the items - that would make a
// container's dimensions a function of its contents, resizing as items move, and would hide
// exactly the bug this reports.
game::Size GridSize(game::ItemLocation container) {
    return game::GetContainerGridSize(container).value_or(game::Size::Zero);
}

// Fingerprint of a container, built from the same traversal that produces the payload so
// it can't miss a field. Detail::Structural keeps it Description()-free and leaves out
// the stats that tick in place (durability, quantity). The dimensions are part of it so a
// grid that only resolves once the game has populated its layout still re-sends.
size_t ContainerHash(game::Size dims, const std::vector<game::Unit>& items) {
    json structural = json::array();
    structural.push_back({dims.width, dims.height});
    for (const auto& item : items) {
        structural.push_back(UnitToJson(item, Detail::Structural));
    }
    return std::hash<std::string>{}(structural.dump());
}

json BuildContainer(size_t bucket, const std::vector<game::Unit>& items, game::Size dims) {
    json itemsArr = json::array();
    for (const auto& item : items) {
        itemsArr.push_back(UnitToJson(item));
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

json BuildIdentity() {
    json identity = json::object();
    identity["account"] = game::GetAccountName();
    identity["realm"] = game::GetRealmShort();
    identity["difficulty"] = game::GetDifficulty();
    identity["charFlags"] = game::GetCharFlags();
    // hardcore/expansion are derivable from charFlags; ladder is a separate BnetData
    // flag, so it stays here.
    identity["ladder"] = game::IsLadder().value_or(0) != 0;
    return identity;
}

// The active difficulty's completed quests + waypoints (identity.difficulty tags which).
json BuildProgression() {
    json progression = json::object();

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

// True when the section moved (and so has to ride this snapshot), storing the new
// fingerprint. Every section is fingerprinted independently, so a snapshot carries only
// what changed and the manager merges it in by key.
bool TakeIfChanged(bool keyframe, size_t hash, std::optional<size_t>& fingerprint) {
    if (!keyframe && fingerprint.has_value() && *fingerprint == hash) {
        return false;
    }
    fingerprint = hash;
    return true;
}

// Keyframes carry every container (including empty) so the client has the full set and
// grid sizes; steady state re-sends one only when its contents change.
void EmitContainer(json& containers, std::string_view name, size_t bucket, const std::vector<game::Unit>& items,
                   game::Size dims, bool keyframe, size_t hash, std::optional<size_t>& fingerprint) {
    if (!TakeIfChanged(keyframe, hash, fingerprint)) {
        return;
    }
    containers[std::string(name)] = BuildContainer(bucket, items, dims);
}

// A wearer section: its unit fields (one fingerprint, all-or-nothing) with `containers`
// alongside (one fingerprint each). Empty when nothing about the wearer moved, so the
// caller omits the key entirely and the manager keeps what it has.
json BuildWearerSection(json&& unitDoc, bool unitChanged, json&& stats, bool statsChanged, json&& containers) {
    json out = unitChanged ? std::move(unitDoc) : json::object();
    if (statsChanged) {
        out["stats"] = std::move(stats);
    }
    if (!containers.empty()) {
        out["containers"] = std::move(containers);
    }
    return out;
}

// Compact kill-counter block (a delta since the last send - see the OnTick emit).
// byClass entries are {id, spec, count} bucketed by (class id, SpecType), so each
// rarity of a class is counted separately - spec is the same bitfield as JS
// unit.spectype (0x02 champion, 0x04 unique pack, 0x08 minion, 0 normal).
// bySuperUnique entries are {id, count} keyed by SuperUniques.txt index. Same flat
// array-of-objects shape as stats/skills, ids numeric. The two buckets are disjoint
// (a super-unique is counted only under bySuperUnique, never under byClass), and the
// manager just adds the deltas to its own tally. std::map iteration is key-ordered,
// so the output is sorted and deterministic.
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

size_t HashOf(const json& value) {
    return std::hash<std::string>{}(value.dump());
}

}  // namespace

CharacterState& CharacterState::Instance() {
    static CharacterState instance;
    return instance;
}

void CharacterState::OnTick(game::GameState state, bool sessionEntered) {
    static_assert(BUCKET_COUNT == CONTAINER_COUNT);

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
        progressionFingerprint_.reset();
        playerFingerprint_.reset();
        playerStatsFingerprint_.reset();
        mercFingerprint_.reset();
        mercStatsFingerprint_.reset();
        for (auto& fingerprint : containerFingerprints_) {
            fingerprint.reset();
        }
    }

    // Bucket every owned item once (cheap reads only); the costly per-item build is
    // deferred to BuildContainer for the containers that actually changed.
    std::vector<game::Unit> equipped;
    std::vector<game::Unit> merc;
    std::vector<game::Unit> inventory;
    std::vector<game::Unit> cube;
    std::vector<game::Unit> belt;
    std::vector<game::Unit> stash;
    for (const auto& item : player.GetItems()) {
        const auto loc = item.ItemLocation();
        std::vector<game::Unit>* bucket = nullptr;
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
        bucket->push_back(item);
    }

    // FindMerc walks the monster table; cheap at the ~1s cadence. Merc carries only
    // equipped items.
    const auto mercUnit = player.FindMerc();
    if (mercUnit) {
        for (const auto& item : mercUnit->GetItems()) {
            if (item.ItemLocation() == game::ItemLocation::Equip) {
                merc.push_back(item);
            }
        }
    }

    // Build every section fingerprint first, so the debounce can tell whether the
    // state is still changing before we commit to diffing or sending anything.
    json identity = BuildIdentity();
    const size_t identityHash = HashOf(identity);
    json progression = BuildProgression();
    const size_t progressionHash = HashOf(progression);
    json playerUnit = UnitToJson(player);
    const size_t playerHash = HashOf(playerUnit);
    json playerStats = WearerStats(player);
    const size_t playerStatsHash = HashOf(playerStats);
    // Null rather than absent when there is no merc, so the manager sees the dismissal
    // instead of holding the last one forever.
    json mercUnitJson = mercUnit ? UnitToJson(*mercUnit) : json();
    const size_t mercHash = HashOf(mercUnitJson);
    json mercStats = mercUnit ? WearerStats(*mercUnit) : json();
    const size_t mercStatsHash = HashOf(mercStats);
    // Belt is not a grid panel - its items carry the belt slot in x, so neither the
    // inventory.txt layout nor the item extents describe it.
    const std::array<game::Size, BUCKET_COUNT> containerDims = {game::Size::Zero,
                                                                game::Size::Zero,
                                                                GridSize(game::ItemLocation::Inventory),
                                                                GridSize(game::ItemLocation::Cube),
                                                                {.width = 4, .height = 4},
                                                                GridSize(game::ItemLocation::Stash)};
    const std::array containerHashes = {
        ContainerHash(containerDims[BUCKET_EQUIPPED], equipped),   ContainerHash(containerDims[BUCKET_MERC], merc),
        ContainerHash(containerDims[BUCKET_INVENTORY], inventory), ContainerHash(containerDims[BUCKET_CUBE], cube),
        ContainerHash(containerDims[BUCKET_BELT], belt),           ContainerHash(containerDims[BUCKET_STASH], stash)};

    // Debounce: combine the slow-moving section fingerprints into one signature.
    // While it differs from the previous sample the state is still settling, so
    // remember it and wait; only once it stops changing do we diff + send. The wearer
    // documents are deliberately left out: their experience/gold/hp tick continuously
    // while farming and would never let the signature settle, starving every send. Each
    // instead rides its own fingerprint below and flows at the ~1s cadence. Keyframes
    // bypass the wait entirely.
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

    // Sections only; the envelope keys go on at the end so an untouched snapshot is
    // literally empty and there is no "did anything change" bookkeeping to get wrong.
    json snapshot = json::object();

    if (TakeIfChanged(keyframe, identityHash, identityFingerprint_)) {
        snapshot["identity"] = std::move(identity);
    }
    if (TakeIfChanged(keyframe, progressionHash, progressionFingerprint_)) {
        snapshot["progression"] = std::move(progression);
    }

    const bool playerChanged = TakeIfChanged(keyframe, playerHash, playerFingerprint_);
    json playerContainers = json::object();
    EmitContainer(playerContainers, "equipped", BUCKET_EQUIPPED, equipped, containerDims[BUCKET_EQUIPPED], keyframe,
                  containerHashes[BUCKET_EQUIPPED], containerFingerprints_[BUCKET_EQUIPPED]);
    EmitContainer(playerContainers, "inventory", BUCKET_INVENTORY, inventory, containerDims[BUCKET_INVENTORY], keyframe,
                  containerHashes[BUCKET_INVENTORY], containerFingerprints_[BUCKET_INVENTORY]);
    EmitContainer(playerContainers, "cube", BUCKET_CUBE, cube, containerDims[BUCKET_CUBE], keyframe,
                  containerHashes[BUCKET_CUBE], containerFingerprints_[BUCKET_CUBE]);
    EmitContainer(playerContainers, "belt", BUCKET_BELT, belt, containerDims[BUCKET_BELT], keyframe,
                  containerHashes[BUCKET_BELT], containerFingerprints_[BUCKET_BELT]);
    EmitContainer(playerContainers, "stash", BUCKET_STASH, stash, containerDims[BUCKET_STASH], keyframe,
                  containerHashes[BUCKET_STASH], containerFingerprints_[BUCKET_STASH]);
    json playerJson = BuildWearerSection(std::move(playerUnit), playerChanged, std::move(playerStats),
                                         TakeIfChanged(keyframe, playerStatsHash, playerStatsFingerprint_),
                                         std::move(playerContainers));
    if (!playerJson.empty()) {
        snapshot["player"] = std::move(playerJson);
    }

    // Run the merc container through the fingerprint even with no merc, so it settles
    // to "empty" and a later merc with identical gear still re-sends.
    json mercContainers = json::object();
    EmitContainer(mercContainers, "equipped", BUCKET_MERC, merc, containerDims[BUCKET_MERC], keyframe,
                  containerHashes[BUCKET_MERC], containerFingerprints_[BUCKET_MERC]);
    const bool mercChanged = TakeIfChanged(keyframe, mercHash, mercFingerprint_);
    const bool mercStatsChanged = TakeIfChanged(keyframe, mercStatsHash, mercStatsFingerprint_);
    if (!mercUnit) {
        // Containers and stats would describe a merc that is gone, so the whole wearer
        // goes null.
        if (mercChanged) {
            snapshot["merc"] = json();
        }
    } else if (json mercJson = BuildWearerSection(std::move(mercUnitJson), mercChanged, std::move(mercStats),
                                                  mercStatsChanged, std::move(mercContainers));
               !mercJson.empty()) {
        snapshot["merc"] = std::move(mercJson);
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
    }

    if (snapshot.empty()) {
        return;  // nothing moved - a header-only message tells the manager nothing
    }

    snapshot["schemaVersion"] = SCHEMA_VERSION;
    snapshot["gameId"] = gameId_;
    snapshot["keyframe"] = keyframe;
    // Wall-clock time the snapshot was assembled (Unix epoch ms) so the manager
    // can show "last updated" from the game side rather than its receive time.
    snapshot["updatedAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

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
    // (Pindleskin, Eldritch, ...) is tracked only by its SuperUniques.txt index;
    // everything else (trash, champions, random uniques, act bosses) by {class id,
    // SpecType} so its rarity is preserved. The maps hold the unsent delta - OnTick
    // emits and clears them.
    if (const auto superUnique = monster->SuperUniqueId()) {
        ++killsBySuperUnique_[*superUnique];
    } else {
        ++killsByClass_[{monster->ClassId(), monster->SpecType()}];
    }
}

}  // namespace d2bs::js::characterstate
