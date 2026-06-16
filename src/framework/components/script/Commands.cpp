#include "components/script/Commands.h"

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "components/config/AppConfig.h"
#include "components/profile/ProfileService.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "components/script/ScriptTypes.h"
#include "game/GameHelpers.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::framework::script {

namespace {

// Cached logger - GetLogger is a registry lookup, not free.
const std::shared_ptr<spdlog::logger>& Logger() {
    static auto logger = d2bs::utils::GetLogger("command");
    return logger;
}

// Start the current starter script (matches reference's .start behavior -
// reference/d2bs/Helpers.cpp:224-229 + 262-267). Looks up the profile's
// DefaultStarterScript (or DefaultGameScript when in-game) and spawns it.
// The game-state branch reflects reference/d2bs/Helpers.cpp:210-217.
void StartStarter() {
    auto paths = d2bs::config::GetAppConfig().GetScriptPaths();

    // Reference picks szDefault while in-game and szStarter while on the menu.
    const bool inGame = d2bs::game::GetGameState() == d2bs::game::GameState::InGame;
    const std::string& name = inGame ? paths.gameScript : paths.starterScript;
    if (name.empty()) {
        Logger()->warn("No starter script configured");
        return;
    }

    auto mode = inGame ? d2bs::ScriptMode::InGame : d2bs::ScriptMode::OutOfGame;

    auto path = paths.basePath / name;
    auto script = d2bs::ScriptEngine::Instance().StartScript(path, mode);
    if (script) {
        Logger()->info("Started {}", name);
    } else {
        Logger()->warn("Failed to start {}", name);
    }
}

// Mirrors reference/d2bs/Helpers.cpp:279-285.
void LoadScript(std::string_view scriptName) {
    if (scriptName.empty()) {
        Logger()->warn("load: missing script name");
        return;
    }
    auto paths = d2bs::config::GetAppConfig().GetScriptPaths();
    auto mode = (d2bs::game::GetGameState() == d2bs::game::GameState::InGame) ? d2bs::ScriptMode::InGame
                                                                              : d2bs::ScriptMode::OutOfGame;
    auto path = paths.basePath / std::string(scriptName);
    auto script = d2bs::ScriptEngine::Instance().StartScript(path, mode);
    if (script) {
        Logger()->info("Started {}", scriptName);
    } else {
        Logger()->warn("Failed to start {}", scriptName);
    }
}

// Useful for diagnosing hangs: shows all thread stacks including what scripts are blocked on.
void DumpAllStacks() {
    const auto tids = d2bs::thread_utils::EnumerateProcessThreads();
    for (uint32_t tid : tids) {
        const auto name = d2bs::thread_utils::GetThreadDescription(tid);
        const auto trace = d2bs::thread_utils::GetThreadStacktrace(tid, /*skip=*/0);
        Logger()->info("--- thread tid={:#x} name='{}' ---\n{}", tid, name, trace);
    }
    Logger()->info("stacks: dumped {} threads", tids.size());
}

}  // namespace

// .reload - stop all, brief settle, re-start starter.
// Matches reference/d2bs/Helpers.cpp:231-250 (Reload).
void ReloadAll() {
    Logger()->info("Stopping all scripts");
    d2bs::ScriptEngine::Instance().StopAllScripts();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(500ms);  // reference uses Sleep(500) to let things catch up

    // Reference Helpers.cpp:243-249 skips the starter-script launch while the
    // waitForProfile latch is set - the pending profile::Switch will pick
    // the per-profile starter, and kicking one off here would race that.
    if (d2bs::config::GetAppConfig().waitForProfile.load()) {
        return;
    }
    StartStarter();
}

void RunCommand(const std::string& line) {
    auto parts = d2bs::utils::Split(line, " \t", /*maxTokens=*/2);
    if (parts.empty()) {
        return;  // empty or whitespace-only
    }
    auto cmd = d2bs::utils::ToLower(std::move(parts[0]));
    std::string_view args = parts.size() > 1 ? std::string_view{parts[1]} : std::string_view{};

    if (cmd == "start") {
        StartStarter();
        return;
    }
    if (cmd == "stop") {
        d2bs::ScriptEngine::Instance().StopAllScripts();
        return;
    }
    if (cmd == "flush") {
        // Reference flushes the script cache (Helpers.cpp:274-278). d2bsng
        // has no script cache today - no-op for now, but .flush remains a
        // valid (and documented) built-in so users don't see "unknown command"
        // behavior diverging from reference.
        return;
    }
    if (cmd == "reload") {
        ReloadAll();
        return;
    }
    if (cmd == "stacks") {
        DumpAllStacks();
        return;
    }
    if (cmd == "load") {
        LoadScript(args);
        return;
    }
    if (cmd == "profile") {
        // .profile <name> - switch the active profile. GameLoop observes the
        // change on its next tick and reloads per-profile script paths.
        if (args.empty()) {
            Logger()->warn(".profile: missing profile name");
            return;
        }
        auto nameStr = std::string(args);
        if (d2bs::profile::Switch(nameStr)) {
            Logger()->info("switched to {}", nameStr);
        } else {
            Logger()->warn(".profile: profile '{}' not found", nameStr);
        }
        return;
    }
    if (cmd == "exec") {
        // Reference's .exec runs the remainder as JS regardless of any other
        // dispatch policy. Here it's redundant with the fallback below (we
        // always JS-eval on miss), but we keep it so users who learned the
        // reference command still see predictable behavior.
        d2bs::ScriptEngine::Instance().Evaluate(std::string(args));
        return;
    }

    // Fallback: JS eval in the console script's isolate. Use the original
    // (untrimmed) line so stack traces report the expression as the user typed it.
    d2bs::ScriptEngine::Instance().Evaluate(line);
}

}  // namespace d2bs::framework::script
