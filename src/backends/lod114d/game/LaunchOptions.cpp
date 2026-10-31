#include "game/LaunchOptions.h"

#include <Windows.h>

#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "config/OptionParser.h"
#include "utils/utils.h"

namespace d2bs::game {

namespace {

// scripts/extract_api.py reads the builder below - each Add's "-name" and
// "<syntax>" string literals and its OptionText - into the docs, and a host
// lists the same table through GetLaunchOptionSpecs. This table is therefore
// the single source of truth for parsing, help text and documentation.
std::vector<core::config::OptionSpec<LaunchOptions>> BuildSpecs() {
    core::config::SpecBuilder<LaunchOptions> builder;

    builder.Add(
        "-profile", "<name>",
        {.description = "Select the D2BotNG profile to launch with. The framework reads it back through getProfile / "
                        "GetLaunchProfile and uses it to name per-instance resources.",
         .category = "Profile"},
        +[](LaunchOptions& o, std::wstring_view v) { o.profile = utils::ToStr(std::wstring{v}, CP_UTF8); });

    builder.Add(
        "-multi", "",
        {.description = "Allow more than one game client to run at once by bypassing Diablo II's single-instance "
                        "window check (and giving each instance its own Battle.net temp path).",
         .category = "Multi-instance"},
        +[](LaunchOptions& o, std::wstring_view) { o.multiInstance = true; });

    builder.Add(
        "-title", "<title>",
        {.description = "Set the game window title so concurrent instances can be told apart and targeted by a "
                        "manager.",
         .category = "Multi-instance",
         .defaultValue = "Diablo II"},
        +[](LaunchOptions& o, std::wstring_view v) { o.windowTitle = std::wstring{v}; });

    builder.Add(
        "-d2c", "<key>",
        {.description = "Classic (Diablo II) CD key to log in to Battle.net with, injected in place of the one stored "
                        "in the registry.",
         .category = "Battle.net"},
        +[](LaunchOptions& o, std::wstring_view v) { o.classicCdKey = utils::ToStr(std::wstring{v}, CP_ACP); });

    builder.Add(
        "-d2x", "<key>",
        {.description = "Expansion (Lord of Destruction) CD key to log in to Battle.net with, injected in place of the "
                        "one stored in the registry.",
         .category = "Battle.net"},
        +[](LaunchOptions& o, std::wstring_view v) { o.lodCdKey = utils::ToStr(std::wstring{v}, CP_ACP); });

    builder.Add(
        "-ftj", "",
        {.description = "Reduce \"failed to join\" game errors by lowering Battle.net's create/join backoff "
                        "threshold so the client retries sooner.",
         .category = "Battle.net"},
        +[](LaunchOptions& o, std::wstring_view) { o.reduceFailToJoin = true; });

    builder.Add(
        "-cachefix", "",
        {.description = "Give each instance its own bncache.dat (a per-window-title or randomised file) so "
                        "concurrent clients don't race on the shared Battle.net cache.",
         .category = "Battle.net"},
        +[](LaunchOptions& o, std::wstring_view) { o.randomizeBnetCache = true; });

    builder.Add(
        "-sleepy", "",
        {.description = "Cap the game's frame loop so it yields the CPU even while the window is focused. "
                        "Diablo II runs its client loop uncapped when in the foreground (pegging a core); this "
                        "makes the in-game and out-of-game loops sleep each frame as they do in the "
                        "background.",
         .category = "Performance"},
        +[](LaunchOptions& o, std::wstring_view) { o.sleepy = true; });

    builder.Add(
        "-noconsole", "",
        {.description = "Run without the d2bs console window: its window and render thread are never created, "
                        "and the console hotkey and hideConsole / showConsole do nothing. Script output still "
                        "reaches the log.",
         .category = "Performance"},
        +[](LaunchOptions& o, std::wstring_view) { o.disableConsole = true; });

    builder.Add(
        "-realm", "name:host",
        {.description = "Add an extra Battle.net realm to the login server list, given as name:host (repeatable). The "
                        "client dials it on the standard BNCS port 6112; it is injected into the in-memory server list "
                        "only, never the registry.",
         .category = "Battle.net"},
        +[](LaunchOptions& o, std::wstring_view v) { o.realms.push_back(utils::ToStr(std::wstring{v}, CP_UTF8)); });

    builder.Add(
        "-proxy", "socks5://[user:password@]host:port",
        {.description = "Route the game's Battle.net and game-server connections through a SOCKS5 proxy.",
         .category = "Network"},
        +[](LaunchOptions& o, std::wstring_view v) { o.proxy = utils::ToStr(std::wstring{v}, CP_UTF8); });

    builder.Add(
        "-noanalytics", "",
        {.description = "Turn off anonymous usage analytics for this launch. Analytics only runs when the build "
                        "was compiled with an Aptabase app key; this flag forces it off even on such a build.",
         .category = "Analytics"},
        +[](LaunchOptions& o, std::wstring_view) { o.disableAnalytics = true; });

    return std::move(builder.specs);
}

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - process-wide cache
LaunchOptions options;
std::once_flag parsed;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace

const std::vector<core::config::OptionSpec<LaunchOptions>>& GetLaunchOptionSpecs() {
    static const std::vector<core::config::OptionSpec<LaunchOptions>> SPECS = BuildSpecs();
    return SPECS;
}

const LaunchOptions& GetLaunchOptions() {
    std::call_once(parsed, [] {
        const auto& specs = GetLaunchOptionSpecs();
        core::config::ParseCommandLine(specs, options);
        // The switches were for us; the game is left with only its own.
        core::config::RemoveCommandLineOptions(specs);
    });
    return options;
}

}  // namespace d2bs::game