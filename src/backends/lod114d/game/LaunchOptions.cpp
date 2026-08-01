#include "game/LaunchOptions.h"

#include <Windows.h>
#include <shellapi.h>

#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "utils/utils.h"

namespace d2bs::game {

namespace {

// One command-line switch: its name, the argument syntax shown in the docs
// (empty for a bare flag), and the handler that folds the parsed value into
// LaunchOptions. A flag's handler is called with an empty value; a value
// option consumes the following argv token and receives it.
//
// scripts/extract_api.py reads the builder below - the Add("-name", "<syntax>")
// string literals plus the preceding /// doc block - into the docs, the same
// way it reads the CompatibilityFlags catalog. This table is therefore the
// single source of truth for both parsing and documentation.
using ApplyFn = void (*)(LaunchOptions&, std::wstring_view);

struct OptionSpec {
    std::string_view name;
    std::string_view syntax;
    ApplyFn apply;

    bool TakesValue() const { return !syntax.empty(); }
};

struct SpecBuilder {
    std::vector<OptionSpec> specs;

    void Add(std::string_view name, std::string_view syntax, ApplyFn apply) {
        specs.push_back({.name = name, .syntax = syntax, .apply = apply});
    }
};

std::vector<OptionSpec> BuildSpecs() {
    SpecBuilder builder;

    /// @description Select the D2BotNG profile to launch with. The framework reads it back through
    /// getProfile / GetLaunchProfile and uses it to name per-instance resources.
    /// @category Profile
    builder.Add(
        "-profile", "<name>",
        +[](LaunchOptions& o, std::wstring_view v) { o.profile = utils::ToStr(std::wstring{v}, CP_UTF8); });

    /// @description Allow more than one game client to run at once by bypassing Diablo II's single-instance window
    /// check (and giving each instance its own Battle.net temp path).
    /// @category Multi-instance
    builder.Add("-multi", "", +[](LaunchOptions& o, std::wstring_view) { o.multiInstance = true; });

    /// @description Set the game window title so concurrent instances can be told apart and targeted by a manager.
    /// Defaults to "Diablo II" when unset.
    /// @category Multi-instance
    builder.Add("-title", "<title>", +[](LaunchOptions& o, std::wstring_view v) { o.windowTitle = std::wstring{v}; });

    /// @description Classic (Diablo II) CD key to log in to Battle.net with, injected in place of the one stored in
    /// the registry.
    /// @category Battle.net
    builder.Add(
        "-d2c", "<key>",
        +[](LaunchOptions& o, std::wstring_view v) { o.classicCdKey = utils::ToStr(std::wstring{v}, CP_ACP); });

    /// @description Expansion (Lord of Destruction) CD key to log in to Battle.net with, injected in place of the one
    /// stored in the registry.
    /// @category Battle.net
    builder.Add(
        "-d2x", "<key>",
        +[](LaunchOptions& o, std::wstring_view v) { o.lodCdKey = utils::ToStr(std::wstring{v}, CP_ACP); });

    /// @description Reduce "failed to join" game errors by lowering Battle.net's create/join backoff threshold so the
    /// client retries sooner.
    /// @category Battle.net
    builder.Add("-ftj", "", +[](LaunchOptions& o, std::wstring_view) { o.reduceFailToJoin = true; });

    /// @description Give each instance its own bncache.dat (a per-window-title or randomised file) so concurrent
    /// clients don't race on the shared Battle.net cache.
    /// @category Battle.net
    builder.Add("-cachefix", "", +[](LaunchOptions& o, std::wstring_view) { o.randomizeBnetCache = true; });

    /// @description Add an extra Battle.net realm to the login server list, given as name:host (repeatable). The
    /// client dials it on the standard BNCS port 6112; it is injected into the in-memory server list only, never the
    /// registry.
    /// @category Battle.net
    builder.Add(
        "-realm", "name:host",
        +[](LaunchOptions& o, std::wstring_view v) { o.realms.push_back(utils::ToStr(std::wstring{v}, CP_UTF8)); });

    /// @description Route the game's Battle.net and game-server connections through a SOCKS5 proxy.
    /// @category Network
    builder.Add(
        "-proxy", "socks5://[user:password@]host:port",
        +[](LaunchOptions& o, std::wstring_view v) { o.proxy = utils::ToStr(std::wstring{v}, CP_UTF8); });

    /// @description Turn off anonymous usage analytics for this launch. Analytics only runs when the build was
    /// compiled with an Aptabase app key; this flag forces it off even on such a build.
    /// @category Analytics
    builder.Add("-noanalytics", "", +[](LaunchOptions& o, std::wstring_view) { o.disableAnalytics = true; });

    /// @description Bot-manager user identifier attached to analytics events so framework sessions can be correlated
    /// with a manager account. Overrides the D2BS_ANALYTICS_USER environment variable.
    /// @category Analytics
    builder.Add(
        "-analyticsuser", "<id>",
        +[](LaunchOptions& o, std::wstring_view v) { o.analyticsUser = utils::ToStr(std::wstring{v}, CP_UTF8); });

    return std::move(builder.specs);
}

// Command-line switches are pure ASCII, so compare the wide argv token to the
// narrow spec name without a codepage conversion.
bool EqualsAscii(std::wstring_view token, std::string_view name) {
    if (token.size() != name.size()) {
        return false;
    }
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] != static_cast<wchar_t>(name[i])) {
            return false;
        }
    }
    return true;
}

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - process-wide cache
LaunchOptions options;
std::once_flag parsed;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void Parse(LaunchOptions& out) {
    const auto* cmdLine = GetCommandLineW();
    if (cmdLine == nullptr) {
        return;
    }

    int32_t argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (argv == nullptr) {
        return;
    }

    const auto specs = BuildSpecs();

    // argv[0] is the program name; skip it.
    for (int32_t i = 1; i < argc; ++i) {
        const std::wstring_view token{argv[i]};
        for (const auto& spec : specs) {
            if (!EqualsAscii(token, spec.name)) {
                continue;
            }
            if (spec.TakesValue()) {
                if (i + 1 < argc) {
                    const std::wstring_view value{argv[++i]};
                    if (!value.empty()) {
                        spec.apply(out, value);
                    }
                }
            } else {
                spec.apply(out, {});
            }
            break;
        }
    }

    LocalFree(static_cast<void*>(argv));
}

}  // namespace

const LaunchOptions& GetLaunchOptions() {
    std::call_once(parsed, [] { Parse(options); });
    return options;
}

}  // namespace d2bs::game
