#include "components/gameloop/GameLoop.h"

#include <thread>

#include <spdlog/spdlog.h>

#include "components/characterstate/CharacterState.h"
#include "components/config/AppConfig.h"
#include "components/drawing/Drawable.h"
#include "components/events/EventDispatch.h"
#include "components/profile/ProfileService.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "components/script/ScriptTypes.h"
#include "components/speedhack/Speedhack.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/GameThread.h"
#include "game/HandleCache.h"
#include "game/Unit.h"
#include "utils/utils.h"

namespace d2bs::framework::gameloop {

GameLoop::GameLoop() {
    logger_ = d2bs::utils::GetLogger("loop");
}

GameLoop& GameLoop::Instance() {
    static GameLoop instance;
    return instance;
}

std::optional<std::chrono::steady_clock::time_point> GameLoop::GameStartTime() const {
    auto anchor = gameStartedAt_.load(std::memory_order_acquire);
    if (anchor == std::chrono::steady_clock::time_point{}) {
        return std::nullopt;
    }
    return anchor;
}

#ifdef D2BS_TEST_HOOKS
void GameLoop::ResetForTesting() {
    previous_ = Snapshot{};
    gameStartedAt_.store({}, std::memory_order_release);
    writeLockHeld_ = false;
    // OnSleep leaves the game-thread write lock held on exit via the manual
    // Acquire/Release lifecycle. Reset that so each TEST_CASE starts with no
    // lock held, matching writeLockHeld_=false (first tick does not Release).
    if (d2bs::game::GameWriteLock::IsHeldByCurrentThread()) {
        d2bs::game::GameWriteLock::Release();
    }
}
#endif

void GameLoop::OnSleep(std::chrono::milliseconds duration) {
    // Defensive: a Sleep arriving on the game thread before
    // ScriptEngine::Initialize completes should be a no-op. The hook may be
    // installed before engine init in some Framework init orderings.
    if (!d2bs::ScriptEngine::Instance().IsInitialized()) {
        return;
    }

    // Write-lock lifecycle across the tick body + drain loop:
    //
    //   first tick (writeLockHeld_=false - framework has no lock yet):
    //     <body runs unlocked>
    //     <drain loop>             // Acquire/Release around each 1ms slice
    //     Acquire()                // lock held from here on; writeLockHeld_=true
    //
    //   subsequent ticks (writeLockHeld_=true):
    //     <body runs under write lock>
    //     Release()
    //     <drain loop>             // sleep_for(1ms) + Acquire/Drain/Release per slice
    //     Acquire()
    //
    // The exclusive window is the game frame body - the window where the
    // game thread is already the sole writer. Releasing between slices gives
    // script readers a natural window to acquire GameReadLock; draining each
    // slice lets script-posted GameThread work land promptly rather than
    // waiting for the whole sleep duration.
    const auto deadline = std::chrono::steady_clock::now() + duration;

    Snapshot cur;
    d2bs::game::InvalidateHandles();
    TakeSnapshot(cur);

    // Carry the session latch forward across transient states. `InGame` sets
    // it, `Menu` clears it; `Null`/`Busy` leave it alone - that's the entire
    // fix for the transient-state restart loop. Downstream consumers
    // (EvaluateMaxGameTime, DriveScriptLifecycle) compute entry/exit from
    // `prev.inSession` vs `cur.inSession` the same way they already compute
    // `stateChanged`, `profileChanged`, etc.
    cur.inSession = previous_.inSession;
    if (cur.state == d2bs::game::GameState::InGame) {
        cur.inSession = true;
    } else if (cur.state == d2bs::game::GameState::Menu) {
        cur.inSession = false;
    }

    EvaluateChicken(cur);
    EvaluateMaxGameTime(previous_, cur);
    EmitStateEvents(previous_, cur);
    // Live character state to the manager. Runs here (game thread, write lock
    // held) so reads are consistent; self-throttles and diffs internally.
    characterstate::CharacterState::Instance().OnTick(cur.state, !previous_.inSession && cur.inSession);
    DriveScriptLifecycle(previous_, cur);

    d2bs::game::GameThread::Drain();

    previous_ = cur;

    if (writeLockHeld_) {
        d2bs::game::GameWriteLock::Release();
        // Slice the wait into wall-1ms chunks at low speed (gives script
        // readers periodic windows) but yield once the remaining virtual
        // budget is below 1ms wall - at high speed sleeping 1ms wall would
        // overshoot the deadline by far more than the slice itself.
        const float speed = d2bs::speedhack::GetSpeed();
        while (true) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }
            const auto remainingVirtMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (static_cast<float>(remainingVirtMs) < speed) {
                std::this_thread::yield();
            } else {
                d2bs::speedhack::SpeedhackDisabledScope realWaits;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            d2bs::game::GameWriteLock lock;
            d2bs::game::GameThread::Drain();
        }
    }
    d2bs::game::GameWriteLock::Acquire();
    writeLockHeld_ = true;
}

void GameLoop::OnDraw() const {
    // Fires from the render function, before the next OnSleep has taken a new
    // snapshot - previous_.state is the most recently observed game state.
    drawing::Drawable::DrawAll(previous_.state);
}

void GameLoop::EvaluateChicken(const Snapshot& cur) {
    // Only evaluate in-game and when a player was observed.
    if (cur.state != d2bs::game::GameState::InGame || !cur.hp.has_value() || !cur.mp.has_value()) {
        return;
    }

    // Town exclusion: HP/MP thresholds do not trigger in town (reference
    // D2Handlers.cpp:75 gates the HP/MP branch on !IsTownByLevelNo).
    if (d2bs::game::IsTownByLevelNo(cur.areaId)) {
        return;
    }

    auto& cfg = d2bs::config::GetAppConfig();

    // Reference (D2Handlers.cpp:75-76) compares the configured threshold
    // against absolute HP/MP returned by GetUnitHP/GetUnitMP (STAT_HP >> 8),
    // i.e. the threshold value is in absolute HP/MP units, not percent.
    int32_t chickenHp = cfg.chickenHp.load();
    if (chickenHp > 0 && *cur.hp <= static_cast<uint32_t>(chickenHp)) {
        logger_->info("chicken triggered by HP ({} <= {})", *cur.hp, chickenHp);
        d2bs::game::ExitGame();
        return;
    }

    int32_t chickenMp = cfg.chickenMp.load();
    if (chickenMp > 0 && *cur.mp <= static_cast<uint32_t>(chickenMp)) {
        logger_->info("chicken triggered by MP ({} <= {})", *cur.mp, chickenMp);
        d2bs::game::ExitGame();
    }
}

void GameLoop::EvaluateMaxGameTime(const Snapshot& prev, const Snapshot& cur) {
    // Anchor on real game entry; clear on real game exit. Reference d2bs
    // latches `Vars.dwGameTime = GetTickCount()` on game enter and clears it
    // on game leave (Helpers.cpp:142, D2Handlers.cpp:79/100). Transient
    // Null/Busy mid-game leave the anchor alone so `me.gamestarttime` doesn't
    // jump every waypoint use.
    if (!prev.inSession && cur.inSession) {
        gameStartedAt_.store(std::chrono::steady_clock::now(), std::memory_order_release);
    } else if (prev.inSession && !cur.inSession) {
        gameStartedAt_.store({}, std::memory_order_release);
    }

    // Only chicken when we're actually in-game (not during transient
    // Busy/Null) - the underlying ExitGame can't reliably trigger when the
    // game UI is mid-transition.
    if (cur.state != d2bs::game::GameState::InGame) {
        return;
    }

    auto maxGameTime = d2bs::config::GetAppConfig().maxGameTime.load();
    if (maxGameTime.count() <= 0) {
        return;
    }

    auto anchor = gameStartedAt_.load(std::memory_order_acquire);
    if (anchor == std::chrono::steady_clock::time_point{}) {
        // Defensive: we got InGame without observing sessionEntered (latch
        // and anchor desynced via ResetForTesting or similar). Skip rather
        // than firing a bogus elapsed=now() chicken.
        return;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - anchor);
    if (elapsed >= maxGameTime) {
        logger_->info("chicken triggered by maxGameTime ({}ms)", maxGameTime.count());
        d2bs::game::ExitGame();
    }
}

void GameLoop::EmitStateEvents(const Snapshot& prev, const Snapshot& cur) const {
    // Emits once per tick on value-change (not per-packet); intra-tick changes collapse. For per-packet fidelity use
    // addEventListener("gamepacket", ...).

    // HP / MP: emit on first observation and on value change. std::optional
    // comparisons handle "never observed before" without explicit flags.
    if (cur.hp.has_value() && prev.hp != cur.hp) {
        LifeEventDispatch(*cur.hp);
    }
    if (cur.mp.has_value() && prev.mp != cur.mp) {
        ManaEventDispatch(*cur.mp);
    }

    // playerassign: reference only fires on non-null player pointer - never on
    // disappearance. Emit when we have a current id that differs from the
    // previous observation (nullopt -> value, or value -> different value).
    if (cur.playerId.has_value() && prev.playerId != cur.playerId) {
        PlayerAssignEventDispatch(*cur.playerId);
    }
}

void GameLoop::ReloadPathsForProfile(const std::string& name) {
    if (name.empty()) {
        return;
    }
    auto profile = d2bs::profile::Load(name);
    if (!profile) {
        return;
    }

    auto& cfg = d2bs::config::GetAppConfig();
    auto oldPaths = cfg.GetScriptPaths();
    // Start from the settings baseline, not the previously-resolved paths -
    // otherwise overrides from an earlier profile stick when switching to a
    // profile that doesn't set that key.
    auto newPaths = cfg.GetDefaultScriptPaths();
    // Apply non-empty fields from the profile's overrides. basePath is joined
    // against the baseline's parent (so `kolbot` becomes `<install>/kolbot`;
    // absolute override replaces via filesystem::path operator/).
    const auto& overrides = profile->scriptPaths;
    if (!overrides.basePath.empty()) {
        newPaths.basePath = newPaths.basePath.parent_path() / overrides.basePath;
    }
    if (!overrides.starterScript.empty()) {
        newPaths.starterScript = overrides.starterScript;
    }
    if (!overrides.gameScript.empty()) {
        newPaths.gameScript = overrides.gameScript;
    }
    if (!overrides.consoleScript.empty()) {
        newPaths.consoleScript = overrides.consoleScript;
    }
    // Restart the console script only when its resolved name actually changes
    // compared to what was previously running, and only after SetScriptPaths
    // has published the new paths - RestartConsoleScript re-reads GetScriptPaths
    // when picking the path to (re)launch.
    const bool consoleChanged = newPaths.consoleScript != oldPaths.consoleScript;
    cfg.SetScriptPaths(std::move(newPaths));
    if (consoleChanged) {
        d2bs::ScriptEngine::Instance().RestartConsoleScript();
    }
}

void GameLoop::DriveScriptLifecycle(const Snapshot& prev, const Snapshot& cur) {
    // Latch held - launcher hasn't pointed us at a profile yet. Reference
    // Helpers.cpp:316/329 gates starter launches on !Vars.bUseProfileScript.
    if (cur.waitForProfile) {
        return;
    }

    const bool stateChanged = prev.state != cur.state;
    const bool latchCleared = prev.waitForProfile;
    // Profile names preserve user-supplied case at storage; compare case-insensitively
    // so Switch("foo") followed by Switch("FOO") does not trigger a relaunch.
    const bool profileChanged = !d2bs::utils::EqualsCaseInsensitive(prev.profileName, cur.profileName);
    const bool sessionEntered = !prev.inSession && cur.inSession;
    const bool sessionExited = prev.inSession && !cur.inSession;

    // Reload AppConfig.scriptPaths from the new profile on the game thread -
    // single-writer, no RMW race. Also handles console-script restart if the
    // new consoleScript differs.
    if (profileChanged || latchCleared) {
        ReloadPathsForProfile(cur.profileName);
    }

    if (!stateChanged && !latchCleared && !profileChanged) {
        return;
    }

    auto& engine = d2bs::ScriptEngine::Instance();
    auto paths = d2bs::config::GetAppConfig().GetScriptPaths();

    auto startStarter = [&](const std::string& name, d2bs::ScriptMode mode) {
        if (name.empty()) {
            return;
        }
        auto path = paths.basePath / name;
        if (engine.StartScript(path, mode)) {
            logger_->info("started {} ({})", name, mode == d2bs::ScriptMode::InGame ? "InGame" : "OutOfGame");
        } else {
            logger_->warn("failed to start {}", name);
        }
    };

    auto stopMode = [&](d2bs::ScriptMode mode) {
        engine.ForEachScript([mode](const std::shared_ptr<d2bs::Script>& script) {
            if (script->GetMode() == mode) {
                script->Stop();
            }
        });
    };

    // ----- InGame script lifecycle -----
    // Driven by the session latch, not raw `cur.state == InGame` - transient
    // Null/Busy round-trips (waypoint UI, area loads) leave the latch alone
    // so the in-game script keeps running across them. Profile-change
    // mid-game is the only other valid restart trigger.
    if (sessionExited) {
        stopMode(d2bs::ScriptMode::InGame);
    }
    if (sessionEntered || (profileChanged && cur.state == d2bs::game::GameState::InGame)) {
        stopMode(d2bs::ScriptMode::InGame);
        startStarter(paths.gameScript, d2bs::ScriptMode::InGame);
    }

    // ----- OutOfGame (menu) script lifecycle -----
    // OOG persists across Menu<->InGame; only (re)launch the menu starter on
    // startup (Null->Menu), latch-clear, or explicit profile change. The
    // first Menu entry is additionally gated on AppConfig.startAtMenu so
    // launchers that want to defer starter selection to a user action can
    // suppress auto-start without setting waitForProfile.
    if (cur.state == d2bs::game::GameState::Menu) {
        // firstMenuEntry: non-InGame predecessor - captures Null->Menu and
        // any future Busy->Menu transitions.
        const bool firstMenuEntry = prev.state != d2bs::game::GameState::InGame;
        const bool userRequested = latchCleared || profileChanged;
        const bool autoStartAllowed = firstMenuEntry && d2bs::config::GetAppConfig().startAtMenu.load();
        if (userRequested || autoStartAllowed) {
            stopMode(d2bs::ScriptMode::OutOfGame);
            startStarter(paths.starterScript, d2bs::ScriptMode::OutOfGame);
        }
    }
}

void GameLoop::TakeSnapshot(Snapshot& out) {
    out.state = d2bs::game::GetGameState();

    auto& cfg = d2bs::config::GetAppConfig();
    out.waitForProfile = cfg.waitForProfile.load(std::memory_order_acquire);
    out.profileName = cfg.GetProfileName();

    auto player = d2bs::game::Unit::Player();
    if (!player) {
        return;
    }

    out.playerId = player.Id();
    out.hp = player.Hp();
    out.mp = player.Mp();
    out.areaId = player.Area();
}

}  // namespace d2bs::framework::gameloop
