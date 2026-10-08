#include "components/script/Commands.h"

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "components/profile/ProfileService.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "components/script/ScriptTypes.h"
#include "config/AppConfig.h"
#include "game/GameHelpers.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::runtime::script {

namespace {

// Cached logger - GetLogger is a registry lookup, not free.
spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("script.command");
    return *LOGGER;
}

// Start the current starter script (matches reference's .start behavior -
// reference/d2bs/Helpers.cpp:224-229 + 262-267). Looks up the profile's
// DefaultStarterScript (or DefaultGameScript when in-game) and spawns it.
// The game-state branch reflects reference/d2bs/Helpers.cpp:210-217.
void StartStarter() {
    auto paths = core::config::GetAppConfig().GetScriptPaths();

    // Reference picks szDefault while in-game and szStarter while on the menu.
    const bool inGame = game::GetGameState() == game::GameState::InGame;
    const std::string& name = inGame ? paths.gameScript : paths.starterScript;
    if (name.empty()) {
        Log().warn("No starter script configured");
        return;
    }

    auto mode = inGame ? ScriptMode::InGame : ScriptMode::OutOfGame;

    auto path = paths.basePath / name;
    auto script = ScriptEngine::Instance().StartScript(path, mode);
    if (script) {
        Log().info("Started {}", name);
    } else {
        Log().warn("Failed to start {}", name);
    }
}

// Mirrors reference/d2bs/Helpers.cpp:279-285.
void LoadScript(std::string_view scriptName) {
    if (scriptName.empty()) {
        Log().warn("load: missing script name");
        return;
    }
    auto paths = core::config::GetAppConfig().GetScriptPaths();
    auto mode = (game::GetGameState() == game::GameState::InGame) ? ScriptMode::InGame : ScriptMode::OutOfGame;
    auto path = paths.basePath / std::string(scriptName);
    auto script = ScriptEngine::Instance().StartScript(path, mode);
    if (script) {
        Log().info("Started {}", scriptName);
    } else {
        Log().warn("Failed to start {}", scriptName);
    }
}

// Useful for diagnosing hangs: shows all thread stacks including what scripts are blocked on.
void DumpAllStacks() {
    const auto tids = utils::threads::EnumerateProcessThreads();
    for (uint32_t tid : tids) {
        const auto name = utils::threads::GetThreadDescription(tid);
        const auto trace = utils::threads::GetThreadStacktrace(tid, /*skip=*/0);
        Log().info("--- thread tid={:#x} name='{}' ---\n{}", tid, name, trace);
    }
    Log().info("stacks: dumped {} threads", tids.size());
}

}  // namespace

// .reload - stop all, brief settle, re-start starter.
// Matches reference/d2bs/Helpers.cpp:231-250 (Reload).
void ReloadAll() {
    Log().info("Stopping all scripts");
    ScriptEngine::Instance().StopAllScripts();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(500ms);  // reference uses Sleep(500) to let things catch up

    // Reference Helpers.cpp:243-249 skips the starter-script launch while the
    // waitForProfile latch is set - the pending profile::Switch will pick
    // the per-profile starter, and kicking one off here would race that.
    if (core::config::GetAppConfig().waitForProfile.load()) {
        return;
    }
    StartStarter();
}

namespace {

// Runs `line` if its first token names a built-in; returns false (running nothing) otherwise.
bool RunBuiltin(const std::string& line) {
    auto parts = utils::Split(line, " \t", /*maxTokens=*/2);
    if (parts.empty()) {
        return false;
    }
    auto cmd = utils::ToLower(std::move(parts[0]));
    std::string_view args = parts.size() > 1 ? std::string_view{parts[1]} : std::string_view{};

    if (cmd == "start") {
        StartStarter();
        return true;
    }
    if (cmd == "stop") {
        ScriptEngine::Instance().StopAllScripts();
        return true;
    }
    if (cmd == "flush") {
        // Reference flushes the script cache (Helpers.cpp:274-278). d2bsng
        // has no script cache today - no-op for now, but .flush remains a
        // valid (and documented) built-in so users don't see "unknown command"
        // behavior diverging from reference.
        return true;
    }
    if (cmd == "reload") {
        ReloadAll();
        return true;
    }
    if (cmd == "stacks") {
        DumpAllStacks();
        return true;
    }
    if (cmd == "load") {
        LoadScript(args);
        return true;
    }
    if (cmd == "profile") {
        // .profile <name> - switch the active profile. GameLoop observes the
        // change on its next tick and reloads per-profile script paths.
        if (args.empty()) {
            Log().warn(".profile: missing profile name");
            return true;
        }
        auto nameStr = std::string(args);
        if (profile::Switch(nameStr)) {
            Log().info("switched to {}", nameStr);
        } else {
            Log().warn(".profile: profile '{}' not found", nameStr);
        }
        return true;
    }
    if (cmd == "exec") {
        // Reference's .exec runs the remainder as JS regardless of any other
        // dispatch policy. On the console it is redundant with the eval
        // fallback; from chat it is the only way to evaluate JS.
        ScriptEngine::Instance().Evaluate(std::string(args));
        return true;
    }
    return false;
}

}  // namespace

void RunCommand(const std::string& line) {
    if (line.find_first_not_of(" \t") == std::string::npos || RunBuiltin(line)) {
        return;
    }
    // Fallback: JS eval in the console script's isolate. Use the original
    // (untrimmed) line so stack traces report the expression as the user typed it.
    ScriptEngine::Instance().Evaluate(line);
}

bool RunChatCommand(const std::string& line) {
    return RunBuiltin(line);
}

}  // namespace d2bs::runtime::script
