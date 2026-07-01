#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "game/Types.h"

// NOLINTBEGIN(readability-identifier-naming) - spdlog::logger is upstream API naming
namespace spdlog {
class logger;
}  // namespace spdlog
// NOLINTEND(readability-identifier-naming)

namespace d2bs::js::gameloop {

// Per-tick snapshot of the bits of game state the framework diffs across
// frames. Populated under GameWriteLock inside GameLoop::TakeSnapshot.
// `playerId`/`hp`/`mp` are optional so diff logic distinguishes "never
// observed" from "observed but zero", and emission fires only on first
// observation plus later value changes.
struct Snapshot {
    // Null sentinel: "no observation yet". The first real tick transitions
    // Null -> (whatever the game is in), which DriveScriptLifecycle treats as
    // a legitimate entry transition.
    game::GameState state = game::GameState::Null;
    std::optional<uint32_t> playerId;
    std::optional<uint32_t> hp;
    std::optional<uint32_t> mp;
    uint32_t areaId = 0;
    bool waitForProfile = false;
    std::string profileName;

    // Game-session latch. Set when we observe `InGame`; cleared when we
    // observe `Menu`. Transient `Null`/`Busy` mid-game (waypoint UI, area
    // loads) leave it unchanged, so brief state oscillations don't look like
    // game exit/re-enter. Derived (not game-observable) - populated in
    // OnSleep after TakeSnapshot by carrying forward `previous_.inSession`
    // and applying the InGame/Menu transitions. Lets callers compute
    // session-entered / session-exited as `!prev.inSession && cur.inSession`
    // and vice-versa, in the same shape as `stateChanged` / `profileChanged`.
    bool inSession = false;
};

// Per-frame driver. Invoked from the per-version Sleep and render hooks via
// the onSleep / onDraw callbacks. Owns HandleCache invalidation, chicken,
// state-event synthesis, drawable flush, GameThread drain, script lifecycle,
// and the game-thread write-lock lifecycle around the real Sleep call.
class GameLoop {
   public:
    static GameLoop& Instance();

    // Anchor latched on Menu->InGame, nullopt when out of game. Matches
    // reference `me.gamestarttime` semantics (reference/d2bs/JSUnit.cpp:106
    // returns `Vars.dwGameTime`, set/cleared alongside the maxGameTime anchor
    // in Helpers.cpp:142, D2Handlers.cpp:79/100). The clock domain matches
    // the JS-visible `getTickCount()` (CoreFunctions.cpp), so script
    // comparisons against `getTickCount()` hold once the caller converts.
    std::optional<std::chrono::steady_clock::time_point> GameStartTime() const;

    // Called once per frame on the game thread from the Sleep hook. Runs
    // per-frame framework work under the game thread's write lock, then drains
    // the game-thread queue in 1ms slices until `duration` elapses, releasing
    // the lock between slices so script readers can make progress. The lock is
    // left held on exit so the next frame body starts under the write lock.
    void OnSleep(std::chrono::milliseconds duration);

    // Called from the game's render function. Flushes drawables for the most
    // recently observed game state.
    void OnDraw() const;

#ifdef D2BS_TEST_HOOKS
    // Drop all accumulated tick state (previous snapshot, game-start anchor) so
    // each TEST_CASE gets a virgin GameLoop. Compiled in only for the
    // js_tests project.
    void ResetForTesting();
#endif

    GameLoop(const GameLoop&) = delete;
    GameLoop& operator=(const GameLoop&) = delete;
    GameLoop(GameLoop&&) = delete;
    GameLoop& operator=(GameLoop&&) = delete;

   private:
    GameLoop();
    ~GameLoop() = default;

    // Logger fetched in constructor - first Instance() call is on the game thread, after sinks are wired.
    inline static std::shared_ptr<spdlog::logger> logger_;

    // Reads the current client state, the player unit's hp/mp/id/area, and the
    // latched waitForProfile / profileName observables from AppConfig into
    // `out`. Caller must hold GameWriteLock.
    void TakeSnapshot(Snapshot& out);

    // Evaluate HP/MP chicken thresholds against `cur` and trigger
    // game::ExitGame() when either fires. Skipped while in town (matches
    // reference D2Handlers.cpp:74-77).
    void EvaluateChicken(const Snapshot& cur);

    // Evaluate the maxGameTime chicken against `cur`. Anchors `gameStartedAt_`
    // on real game entry (`inSession` transitioning false -> true) and clears
    // it on real game exit (true -> false) - transient Null/Busy round-trips
    // mid-game leave the anchor alone, matching reference `Vars.dwGameTime`
    // semantics. Applies in or out of town (reference D2Handlers.cpp:74).
    void EvaluateMaxGameTime(const Snapshot& prev, const Snapshot& cur);

    // Synthesize per-frame game-state events by diffing `prev` against `cur`.
    void EmitStateEvents(const Snapshot& prev, const Snapshot& cur) const;

    // Start / stop starter scripts in response to game-session entry/exit
    // (`inSession` transitions - derived from the latch update in OnSleep,
    // not raw InGame state, so transient Null/Busy round-trips don't restart
    // the in-game script). Also handles latch-clear and profile-name changes;
    // reloads the profile from disk and merges its per-profile overrides into
    // AppConfig.scriptPaths on the game thread (single-writer, no RMW race).
    // Restarts the console script when the merged consoleScript differs.
    void DriveScriptLifecycle(const Snapshot& prev, const Snapshot& cur);

    // Reload the named profile from the active ConfigStore and merge its
    // ScriptPaths overrides into AppConfig.scriptPaths. No-op if the name is
    // empty or the profile isn't found.
    void ReloadPathsForProfile(const std::string& name);

    Snapshot previous_;
    // Atomic so the V8/script-thread reader in `GameStartTime()` and the
    // game-thread writer in `EvaluateMaxGameTime()` don't tear an 8-byte
    // time_point. On 32-bit x86 this lowers to CMPXCHG8B (lock-free).
    std::atomic<std::chrono::steady_clock::time_point> gameStartedAt_;
    // False until the first OnSleep acquires the write lock. Game-thread-only - no sync needed.
    bool writeLockHeld_ = false;
};

}  // namespace d2bs::js::gameloop
