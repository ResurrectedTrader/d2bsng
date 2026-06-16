#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "game/Types.h"

namespace d2bs::framework::characterstate {

// Assembles live character state (identity, stats, progression, equipment,
// inventory, cube, belt, stash, merc) from the current player unit and pushes
// section-partial snapshots - plus a full keyframe on each game-create - to the
// D2BotNG manager over the existing WM_COPYDATA channel as JSON.
//
// Driven from GameLoop::OnSleep, so it runs on the game thread while the frame
// write lock is held: reads are consistent and the per-resolve GameReadLock
// no-ops (see game/GameLock.h). No threads of its own. Self-throttles to ~1s;
// emits immediately on game entry. Sends through game::SendIPC to the manager
// handle stored on AppConfig (seeded from the "Handle" WM_COPYDATA).
class CharacterState {
   public:
    static CharacterState& Instance();

    // One call per game-loop tick. `state` is the current game state; emit only
    // happens InGame. `sessionEntered` is the Menu/Null -> InGame transition and
    // forces a keyframe.
    void OnTick(d2bs::game::GameState state, bool sessionEntered);

    // Records a monster death observed by the client, fed from the game-layer
    // death hook on the game thread (same thread as OnTick, so no locking). The
    // unit is resolved to bucket the kill by (class id, rarity/SpecType) and, for
    // super-uniques, by SuperUniques.txt index; the running totals ride along in
    // the next snapshot. No-op if the unit can't be resolved. Counts reset per game.
    void RecordKill(uint32_t unitId);

    CharacterState(const CharacterState&) = delete;
    CharacterState& operator=(const CharacterState&) = delete;
    CharacterState(CharacterState&&) = delete;
    CharacterState& operator=(CharacterState&&) = delete;

   private:
    CharacterState() = default;
    ~CharacterState() = default;

    // equipped, merc, inventory, cube, belt, stash.
    static constexpr size_t CONTAINER_COUNT = 6;

    std::optional<std::chrono::system_clock::time_point> lastCheck_;
    std::string gameId_;
    std::string lastGameName_;
    // [[maybe_unused]]: referenced only in CharacterState.cpp, so the framework_tests
    // TU (which includes this header via GameLoop.cpp but not the .cpp) flags these
    // trivial scalars as unused. The non-scalar members above don't trip the warning.
    [[maybe_unused]] uint32_t createCounter_ = 0;
    [[maybe_unused]] bool wasInGame_ = false;

    // Last-sent section fingerprints; nullopt means "not yet sent this game".
    std::optional<size_t> identityFingerprint_;
    std::optional<size_t> statsFingerprint_;
    std::optional<size_t> progressionFingerprint_;
    std::array<std::optional<size_t>, CONTAINER_COUNT> containerFingerprints_;

    // Combined fingerprint of the most recently sampled state. The debounce holds
    // off sending until this stops changing between samples.
    std::optional<size_t> pendingHash_;

    // Observed monster kills not yet sent - the pending delta. The death hook
    // accumulates here; OnTick emits the maps and clears them, so the manager only
    // ever adds deltas to its own persistent tally (no per-game reset / gameId
    // bookkeeping). Kept out of the debounce signature so a kill never gates - or is
    // gated by - the other sections. The two buckets are disjoint (super-uniques
    // only in killsBySuperUnique_). std::map iterates in key order, so the wire
    // output is sorted and deterministic.
    //
    // killsByClass_ is keyed by {classId, SpecType} so each rarity (normal /
    // champion / unique pack / minion) of a class is bucketed separately;
    // killsBySuperUnique_ is keyed by SuperUniques.txt index.
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> killsByClass_;
    std::map<uint32_t, uint32_t> killsBySuperUnique_;
};

}  // namespace d2bs::framework::characterstate
