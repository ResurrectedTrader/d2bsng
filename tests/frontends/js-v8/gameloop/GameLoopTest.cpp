#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

#include "components/gameloop/GameLoop.h"
#include "components/profile/ProfileService.h"
#include "components/script/Commands.h"
#include "components/script/ScriptEngine.h"
#include "config/AppConfig.h"
#include "config/IniConfigStore.h"
#include "config/ProfileData.h"
#include "config/ScriptPaths.h"
#include "fakes/GameLoopCollaborators.h"
#include "game/Types.h"
#include "utils/utils.h"

using d2bs::game::GameState;
using d2bs::js::gameloop::GameLoop;

namespace {

// Each TEST_CASE seeds known state via this harness so tests don't inherit
// state from earlier cases. GameLoop::ResetForTesting clears the singleton's
// previous snapshot / first-tick sentinel; GameLoopCollaborators::Reset zeroes
// the captured event vectors and the inputs TakeSnapshot reads.
struct GameLoopFixture {
    GameLoopFixture() {
        GameLoop::Instance().ResetForTesting();
        d2bs::test::Reset();
        d2bs::ScriptEngine::Instance().Reset();
        d2bs::ScriptEngine::Instance().SetInitialized(true);
        auto& cfg = d2bs::config::GetAppConfig();
        cfg.chickenHp.store(0);
        cfg.chickenMp.store(0);
        cfg.maxGameTime.store(std::chrono::milliseconds{0});
        cfg.waitForProfile.store(false);
        cfg.startAtMenu.store(true);
        cfg.SetScriptPaths(d2bs::config::ScriptPaths{});
        cfg.SetDefaultScriptPaths(d2bs::config::ScriptPaths{});
        cfg.SetProfileName("");
        cfg.store.reset();
    }
};

}  // namespace

TEST_CASE_FIXTURE(GameLoopFixture, "First tick emits melife/memana/playerassign on initial observation") {
    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 500;
    s.mp = 200;
    s.areaId = 1;  // Act 1 rogue camp would be isTown=true, but we leave isTown=false to not trigger chicken
    s.isTown = true;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    REQUIRE(s.lifeEvents.size() == 1);
    CHECK(s.lifeEvents[0] == 500);
    REQUIRE(s.manaEvents.size() == 1);
    CHECK(s.manaEvents[0] == 200);
    REQUIRE(s.playerAssignEvents.size() == 1);
    CHECK(s.playerAssignEvents[0] == 42);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Successive ticks with no change emit nothing") {
    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 500;
    s.mp = 200;
    s.areaId = 1;
    s.isTown = true;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    s.lifeEvents.clear();
    s.manaEvents.clear();
    s.playerAssignEvents.clear();

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(s.lifeEvents.empty());
    CHECK(s.manaEvents.empty());
    CHECK(s.playerAssignEvents.empty());
}

TEST_CASE_FIXTURE(GameLoopFixture, "HP change emits melife on second tick") {
    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 500;
    s.mp = 200;
    s.areaId = 1;
    s.isTown = true;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    s.lifeEvents.clear();

    s.hp = 300;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    REQUIRE(s.lifeEvents.size() == 1);
    CHECK(s.lifeEvents[0] == 300);
}

TEST_CASE_FIXTURE(GameLoopFixture, "HP drop below threshold triggers chicken when not in town") {
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.chickenHp.store(100);

    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 500;
    s.mp = 200;
    s.areaId = 10;  // not a town area
    s.isTown = false;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(s.exitGameCount == 0);

    s.hp = 50;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(s.exitGameCount == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Town exclusion suppresses HP chicken") {
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.chickenHp.store(100);

    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 50;  // below threshold on tick 1
    s.mp = 200;
    s.areaId = 1;
    s.isTown = true;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(s.exitGameCount == 0);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Town exclusion does not suppress maxGameTime chicken") {
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.maxGameTime.store(std::chrono::milliseconds{1});  // 1 ms - will be exceeded almost immediately on the 2nd tick

    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 500;
    s.mp = 200;
    s.areaId = 1;
    s.isTown = true;

    // First tick anchors gameStartedAt_.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    // Wait past the 1ms threshold so the next tick fires maxGameTime chicken.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(s.exitGameCount == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Menu->InGame starts default game script") {
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.gameScript = "default.dbj";
    cfg.SetScriptPaths(paths);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    GameLoop::Instance().OnSleep(
        std::chrono::milliseconds{0});  // first tick: Menu, starterScript empty -> nothing starts
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().empty());

    s.clientState = GameState::InGame;
    s.playerId = 1;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetMode() == d2bs::ScriptMode::InGame);
    CHECK(started[0]->GetPath().filename() == "default.dbj");
}

TEST_CASE_FIXTURE(GameLoopFixture, "InGame->Menu stops in-game scripts and preserves OOG starter") {
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.gameScript = "game.dbj";
    paths.starterScript = "starter.dbj";
    cfg.SetScriptPaths(paths);

    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 1;

    // First tick: prev.state == Null (sentinel) and cur.state == InGame triggers the
    // entry-into-InGame transition, starting the in-game script (covers the DLL-injected-mid-game case).
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    auto inGameScript = started[0];
    CHECK(inGameScript->GetMode() == d2bs::ScriptMode::InGame);

    // Transition to Menu - stops the in-game script but does NOT relaunch the
    // OOG starter (reference parity: OOG scripts persist across Menu↔InGame,
    // and the starter is only (re)launched on startup / latch-clear / profile
    // change).
    s.clientState = GameState::Menu;
    s.playerId.reset();
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(inGameScript->IsStopped());
    CHECK(started.size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "OnSleep does not paint drawables") {
    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(s.drawAllCount == 0);
    CHECK_FALSE(s.lastDrawState.has_value());
}

TEST_CASE_FIXTURE(GameLoopFixture, "OnDraw paints drawables with most recently observed state") {
    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    // OnDraw before any OnSleep paints with the default (Null) sentinel state.
    GameLoop::Instance().OnDraw();
    CHECK(s.drawAllCount == 1);
    REQUIRE(s.lastDrawState.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above
    CHECK(*s.lastDrawState == GameState::Null);
    // NOLINTEND(bugprone-unchecked-optional-access)

    // OnSleep updates the snapshot; the next OnDraw picks up the new state.
    s.clientState = GameState::InGame;
    s.playerId = 1;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    GameLoop::Instance().OnDraw();
    CHECK(s.drawAllCount == 2);
    REQUIRE(s.lastDrawState.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above
    CHECK(*s.lastDrawState == GameState::InGame);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE_FIXTURE(GameLoopFixture, "waitForProfile disables script lifecycle") {
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.waitForProfile.store(true);
    d2bs::config::ScriptPaths paths;
    paths.gameScript = "game.dbj";
    paths.starterScript = "starter.dbj";
    cfg.SetScriptPaths(paths);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    s.clientState = GameState::InGame;
    s.playerId = 1;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().empty());
}

namespace {

// RAII temp INI - populates AppConfig.store so ReloadPathsForProfile can load
// profile data from disk. Mirrors the harness in ProfileTest.cpp.
struct TempIniForGameLoop {
    std::filesystem::path path;

    TempIniForGameLoop() {
        auto suffix = std::to_string(reinterpret_cast<uintptr_t>(this));
        path = std::filesystem::temp_directory_path() / ("d2bsng_gameloop_test_" + suffix + ".ini");
        std::ofstream(path.string()).close();
        auto& cfg = d2bs::config::GetAppConfig();
        cfg.store = std::make_unique<d2bs::config::IniConfigStore>(path);
    }

    ~TempIniForGameLoop() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        d2bs::config::GetAppConfig().store.reset();
    }
};

// Write per-profile script-override keys (ScriptPath / DefaultStarterScript /
// DefaultGameScript / DefaultConsoleScript) directly to the INI. SaveProfile
// only writes login-related fields (matching reference); script overrides are
// read-only INI fields, so tests that need them must write them like a user
// hand-editing d2bs.ini. Uses WritePrivateProfileStringW so the keys land in
// the existing [<profileName>] section that profile::Add already created -
// GetPrivateProfileStringW reads only the first occurrence of a section, so
// appending a duplicate section would leave the keys unreachable.
void WriteScriptOverrides(const std::filesystem::path& ini, const std::string& profileName,
                          const d2bs::config::ScriptPaths& paths) {
    std::wstring wideSection = d2bs::utils::ToWStr(profileName);
    std::wstring widePath = ini.wstring();
    auto write = [&](const char* key, const std::string& value) {
        if (value.empty())
            return;
        std::wstring wideKey = d2bs::utils::ToWStr(key);
        std::wstring wideValue = d2bs::utils::ToWStr(value);
        WritePrivateProfileStringW(wideSection.c_str(), wideKey.c_str(), wideValue.c_str(), widePath.c_str());
    };
    write("ScriptPath", paths.basePath.string());
    write("DefaultStarterScript", paths.starterScript);
    write("DefaultGameScript", paths.gameScript);
    write("DefaultConsoleScript", paths.consoleScript);
}

}  // namespace

TEST_CASE_FIXTURE(GameLoopFixture, "Latch cleared via Switch launches starter on next tick") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.waitForProfile.store(true);
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "fallback.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    // Seed a profile with no script overrides so merge leaves AppConfig paths alone.
    d2bs::config::ProfileData prof;
    prof.name = "p1";
    prof.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(prof);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    // First tick: latch still set, no launch.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().empty());

    // Switch clears the latch and sets the name atomically.
    CHECK(d2bs::profile::Switch("p1") == true);

    // Second tick: latch just cleared -> starter launches exactly once.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetMode() == d2bs::ScriptMode::OutOfGame);
    CHECK(started[0]->GetPath().filename() == "fallback.dbj");
}

TEST_CASE_FIXTURE(GameLoopFixture, "CLI launch profile seeds name once - no double launch") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "base.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    // Seed profile whose starter overrides the base.
    d2bs::config::ProfileData prof;
    prof.name = "foo";
    prof.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(prof);
    WriteScriptOverrides(tmp.path, "foo", {.starterScript = "foo_starter.dbj"});

    // Emulate CLI -profile foo landing before first tick.
    CHECK(d2bs::profile::Switch("foo") == true);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    // First real tick: prev state=Null, name went empty->foo, both state and
    // profile change fire together but starter launches only once.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetPath().filename() == "foo_starter.dbj");

    // Further ticks with no change: no additional launches.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Mid-session /profile bar reloads overrides, relaunches OOG starter") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "base.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData bar;
    bar.name = "bar";
    bar.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(bar);
    WriteScriptOverrides(tmp.path, "bar", {.starterScript = "bar_starter.dbj"});

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    // Initial tick launches base.dbj on Null->Menu.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetPath().filename() == "base.dbj");
    auto prevStarter = started[0];

    // Mid-session switch: name changes to "bar".
    CHECK(d2bs::profile::Switch("bar") == true);

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    REQUIRE(started.size() == 2);
    CHECK(prevStarter->IsStopped());
    CHECK(started[1]->GetPath().filename() == "bar_starter.dbj");
    CHECK(started[1]->GetMode() == d2bs::ScriptMode::OutOfGame);
}

TEST_CASE_FIXTURE(GameLoopFixture, "/profile at InGame swaps game script only") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.gameScript = "base_game.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData profA;
    profA.name = "A";
    profA.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(profA);
    WriteScriptOverrides(tmp.path, "a", {.gameScript = "a_game.dbj"});

    d2bs::config::ProfileData profB;
    profB.name = "B";
    profB.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(profB);
    WriteScriptOverrides(tmp.path, "b", {.gameScript = "b_game.dbj"});

    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 1;

    CHECK(d2bs::profile::Switch("A") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetMode() == d2bs::ScriptMode::InGame);
    CHECK(started[0]->GetPath().filename() == "a_game.dbj");
    auto aScript = started[0];

    CHECK(d2bs::profile::Switch("B") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    REQUIRE(started.size() == 2);
    CHECK(aScript->IsStopped());
    CHECK(started[1]->GetMode() == d2bs::ScriptMode::InGame);
    CHECK(started[1]->GetPath().filename() == "b_game.dbj");
    // No OOG scripts started - we never transitioned through Menu.
    for (const auto& sc : started) {
        CHECK(sc->GetMode() == d2bs::ScriptMode::InGame);
    }
}

TEST_CASE_FIXTURE(GameLoopFixture, "Same-profile repeat Switch is a no-op") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "base.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData foo;
    foo.name = "foo";
    foo.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(foo);
    WriteScriptOverrides(tmp.path, "foo", {.starterScript = "foo.dbj"});

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    CHECK(d2bs::profile::Switch("foo") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetPath().filename() == "foo.dbj");

    // Switch to foo again - name unchanged => no relaunch.
    CHECK(d2bs::profile::Switch("foo") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(started.size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Same-profile Switch with different case is a no-op") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "base.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData foo;
    foo.name = "foo";
    foo.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(foo);
    WriteScriptOverrides(tmp.path, "foo", {.starterScript = "foo.dbj"});

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    CHECK(d2bs::profile::Switch("foo") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);

    // Switch to same profile with different case - change detection compares
    // case-insensitively so no relaunch should fire.
    CHECK(d2bs::profile::Switch("FOO") == true);
    CHECK(cfg.GetProfileName() == "FOO");
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(started.size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Sticky overrides revert when switching to profile without override") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "default.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData profileA;
    profileA.name = "A";
    profileA.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(profileA);
    WriteScriptOverrides(tmp.path, "a", {.starterScript = "a_only.dbj"});

    // Profile B has no starterScript override - should revert to settings default.
    d2bs::config::ProfileData profileB;
    profileB.name = "B";
    profileB.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(profileB);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    CHECK(d2bs::profile::Switch("A") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetPath().filename() == "a_only.dbj");
    CHECK(cfg.GetScriptPaths().starterScript == "a_only.dbj");

    CHECK(d2bs::profile::Switch("B") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    REQUIRE(started.size() == 2);
    // Switching to B (no override) must NOT inherit A's override.
    CHECK(started[1]->GetPath().filename() == "default.dbj");
    CHECK(cfg.GetScriptPaths().starterScript == "default.dbj");
}

TEST_CASE_FIXTURE(GameLoopFixture, "startAtMenu=false gates initial Menu starter launch") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.startAtMenu.store(false);
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "starter.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData p;
    p.name = "p";
    p.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(p);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;

    // Null->Menu with startAtMenu=false: no auto-launch.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().empty());

    // User-requested Switch launches starter even with startAtMenu=false.
    CHECK(d2bs::profile::Switch("p") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetPath().filename() == "starter.dbj");
}

TEST_CASE_FIXTURE(GameLoopFixture, "Console restart observes new paths (BUG A regression)") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.consoleScript = "default.js";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData p;
    p.name = "p";
    p.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(p);
    WriteScriptOverrides(tmp.path, "p", {.consoleScript = "profile.js"});

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    d2bs::ScriptEngine::Instance().ResetRestartCount();

    CHECK(d2bs::profile::Switch("p") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK(d2bs::ScriptEngine::Instance().RestartConsoleCount() == 1);
    // AppConfig holds the new consoleScript, and that's what RestartConsoleScript
    // observed at call time - proves SetScriptPaths ran first.
    CHECK(cfg.GetScriptPaths().consoleScript == "profile.js");
    CHECK(d2bs::ScriptEngine::Instance().RestartedConsoleName() == "profile.js");
}

TEST_CASE_FIXTURE(GameLoopFixture, ".reload with waitForProfile=true is a no-op") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.waitForProfile.store(true);
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "starter.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    // No StartedScripts beforehand; ReloadAll stops-all (no-op) + sleeps + the
    // waitForProfile guard returns before StartStarter runs.
    d2bs::js::script::ReloadAll();
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().empty());
}

TEST_CASE_FIXTURE(GameLoopFixture, "Console script restarts only when consoleScript override changes") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.starterScript = "base.dbj";
    paths.consoleScript = "console.js";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData same;
    same.name = "same";
    same.type = d2bs::config::ProfileType::SinglePlayer;
    // No consoleScript override -> merge leaves AppConfig value intact.
    d2bs::profile::Add(same);

    d2bs::config::ProfileData diff;
    diff.name = "diff";
    diff.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(diff);
    WriteScriptOverrides(tmp.path, "diff", {.consoleScript = "custom_console.js"});

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;
    // Initial tick launches starter.
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    d2bs::ScriptEngine::Instance().ResetRestartCount();

    // Switch to profile without override -> no console restart.
    CHECK(d2bs::profile::Switch("same") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(d2bs::ScriptEngine::Instance().RestartConsoleCount() == 0);

    // Switch to profile with an override -> console restart fires once.
    CHECK(d2bs::profile::Switch("diff") == true);
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(d2bs::ScriptEngine::Instance().RestartConsoleCount() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Switch(\"\") is a no-op") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.SetProfileName("foo");
    cfg.waitForProfile.store(true);

    CHECK(d2bs::profile::Switch("") == false);
    // Name unchanged.
    CHECK(cfg.GetProfileName() == "foo");
    // Latch not cleared.
    CHECK(cfg.waitForProfile.load() == true);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Switch(\"nonexistent\") is a no-op") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    cfg.SetProfileName("foo");
    cfg.waitForProfile.store(true);

    CHECK(d2bs::profile::Switch("does_not_exist") == false);
    CHECK(cfg.GetProfileName() == "foo");
    CHECK(cfg.waitForProfile.load() == true);
}

TEST_CASE_FIXTURE(GameLoopFixture, "InGame->Menu->InGame preserves OOG starter") {
    TempIniForGameLoop tmp;
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.basePath = "/scripts";
    paths.gameScript = "game.dbj";
    paths.starterScript = "starter.dbj";
    cfg.SetScriptPaths(paths);
    cfg.SetDefaultScriptPaths(paths);

    d2bs::config::ProfileData p;
    p.name = "p";
    p.type = d2bs::config::ProfileType::SinglePlayer;
    d2bs::profile::Add(p);

    auto& s = d2bs::test::State();

    // Null->Menu: starter launches once.
    s.clientState = GameState::Menu;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    CHECK(started[0]->GetMode() == d2bs::ScriptMode::OutOfGame);
    CHECK(started[0]->GetPath().filename() == "starter.dbj");
    auto starter = started[0];

    // Menu->InGame: gameScript launches.
    s.clientState = GameState::InGame;
    s.playerId = 1;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    REQUIRE(started.size() == 2);
    CHECK(started[1]->GetMode() == d2bs::ScriptMode::InGame);
    CHECK(started[1]->GetPath().filename() == "game.dbj");
    auto game1 = started[1];
    CHECK_FALSE(starter->IsStopped());

    // InGame->Menu: gameScript stops; starter is NOT relaunched.
    s.clientState = GameState::Menu;
    s.playerId.reset();
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(game1->IsStopped());
    CHECK(started.size() == 2);  // no new starter
    CHECK_FALSE(starter->IsStopped());

    // Menu->InGame again: gameScript launches again, starter still alive.
    s.clientState = GameState::InGame;
    s.playerId = 1;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    REQUIRE(started.size() == 3);
    CHECK(started[2]->GetMode() == d2bs::ScriptMode::InGame);
    CHECK(started[2]->GetPath().filename() == "game.dbj");
    CHECK_FALSE(starter->IsStopped());
    CHECK(starter.get() == d2bs::ScriptEngine::Instance().StartedScripts()[0].get());
}

TEST_CASE_FIXTURE(GameLoopFixture, "OnSleep is a no-op pre-Initialize") {
    d2bs::ScriptEngine::Instance().SetInitialized(false);

    auto& s = d2bs::test::State();
    s.clientState = GameState::InGame;
    s.playerId = 42;
    s.hp = 500;
    s.mp = 200;
    s.areaId = 1;
    s.isTown = false;

    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    // No snapshot taken, no events emitted, no scripts started, no draws.
    CHECK(s.lifeEvents.empty());
    CHECK(s.manaEvents.empty());
    CHECK(s.playerAssignEvents.empty());
    CHECK(s.exitGameCount == 0);
    CHECK(s.drawAllCount == 0);
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().empty());
}

// ============================================================================
// Session latch: in-game script must survive transient Null/Busy mid-game.
// The bug these tests guard against: GameState briefly transitions to Null
// (waypoint menu's firstControl != null) or Busy (area-load: player path /
// inventory / level temporarily inconsistent) between InGame ticks, which
// pre-fix caused stop+restart on every wp use -> SoloPlay.js looped on the
// first town errand. The session latch carries `inSession` across these
// transient observations so only Menu cleanly ends a session.
// ============================================================================

namespace {

// Replays Menu -> InGame to leave the fixture in a fully-anchored in-game
// session with one running game script. Returns the running script so tests
// can assert it survives subsequent transitions.
std::shared_ptr<d2bs::Script> EnterGameSession() {
    auto& cfg = d2bs::config::GetAppConfig();
    d2bs::config::ScriptPaths paths;
    paths.gameScript = "game.dbj";
    cfg.SetScriptPaths(paths);

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    s.clientState = GameState::InGame;
    s.playerId = 1;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 1);
    REQUIRE(started[0]->GetMode() == d2bs::ScriptMode::InGame);
    return started[0];
}

}  // namespace

TEST_CASE_FIXTURE(GameLoopFixture, "Transient Null mid-game does not restart (waypoint scenario)") {
    auto inGameScript = EnterGameSession();

    // Simulate waypoint menu opening: GetGameState returns Null when the
    // game's firstControl pointer is non-null (UI overlay on top of player).
    auto& s = d2bs::test::State();
    s.clientState = GameState::Null;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    s.clientState = GameState::InGame;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK_FALSE(inGameScript->IsStopped());
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Transient Busy mid-game does not restart (area-load scenario)") {
    auto inGameScript = EnterGameSession();

    // Simulate area load: GetGameState returns Busy when path/inventory/
    // level pointers are temporarily inconsistent during the level switch.
    auto& s = d2bs::test::State();
    s.clientState = GameState::Busy;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    s.clientState = GameState::InGame;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK_FALSE(inGameScript->IsStopped());
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "Full waypoint cycle InGame -> Null -> Busy -> InGame keeps script alive") {
    auto inGameScript = EnterGameSession();

    // The exact sequence observed in the field logs: wp menu (Null), then
    // destination loading (Busy), then arrival (InGame).
    auto& s = d2bs::test::State();
    s.clientState = GameState::Null;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    s.clientState = GameState::Busy;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    s.clientState = GameState::InGame;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK_FALSE(inGameScript->IsStopped());
    CHECK(d2bs::ScriptEngine::Instance().StartedScripts().size() == 1);
}

TEST_CASE_FIXTURE(GameLoopFixture, "gameStartedAt_ stays anchored across transient Null/Busy") {
    EnterGameSession();
    const auto anchor = GameLoop::Instance().GameStartTime();
    REQUIRE(anchor.has_value());

    auto& s = d2bs::test::State();

    // Round-trip through Null and Busy - the anchor must not move.
    s.clientState = GameState::Null;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(GameLoop::Instance().GameStartTime() == anchor);

    s.clientState = GameState::Busy;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(GameLoop::Instance().GameStartTime() == anchor);

    s.clientState = GameState::InGame;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(GameLoop::Instance().GameStartTime() == anchor);
}

TEST_CASE_FIXTURE(GameLoopFixture, "gameStartedAt_ clears on real game exit to Menu") {
    EnterGameSession();
    REQUIRE(GameLoop::Instance().GameStartTime().has_value());

    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;
    s.playerId.reset();
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    CHECK_FALSE(GameLoop::Instance().GameStartTime().has_value());
}

TEST_CASE_FIXTURE(GameLoopFixture,
                  "Game exit via Busy -> Menu stops script (Menu observation is what ends the session)") {
    auto inGameScript = EnterGameSession();

    // Exits often go through Busy briefly before reaching Menu - the
    // session must NOT end until we actually observe Menu.
    auto& s = d2bs::test::State();
    s.clientState = GameState::Busy;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK_FALSE(inGameScript->IsStopped());  // Busy alone doesn't end the session.

    s.clientState = GameState::Menu;
    s.playerId.reset();
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(inGameScript->IsStopped());  // Menu ends the session, script stops.
}

TEST_CASE_FIXTURE(GameLoopFixture, "Second game in a session: Menu -> InGame after exit starts fresh script") {
    auto first = EnterGameSession();

    // Real exit.
    auto& s = d2bs::test::State();
    s.clientState = GameState::Menu;
    s.playerId.reset();
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});
    CHECK(first->IsStopped());

    // Re-enter - fresh script.
    s.clientState = GameState::InGame;
    s.playerId = 2;
    GameLoop::Instance().OnSleep(std::chrono::milliseconds{0});

    const auto& started = d2bs::ScriptEngine::Instance().StartedScripts();
    REQUIRE(started.size() == 2);
    CHECK_FALSE(started[1]->IsStopped());
    CHECK(started[1]->GetMode() == d2bs::ScriptMode::InGame);

    // New anchor should be fresh (we cleared it on Menu, set it again on
    // Menu -> InGame).
    CHECK(GameLoop::Instance().GameStartTime().has_value());
}
