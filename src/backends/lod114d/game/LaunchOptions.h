#pragma once

#include <optional>
#include <string>
#include <vector>

#include "config/OptionParser.h"

namespace d2bs::game {

// All command-line switches recognised by the d2bs port. Parsed once from
// GetCommandLineW() and cached for the lifetime of the process.
//
// Multi-instance / titling and FTJ/CDKey/CacheFix patches in
// hooks::intercepts read these directly at install time; framework consumers
// see only `profile` (via GetLaunchProfile).
struct LaunchOptions {
    std::optional<std::string> profile;  // -profile <name>

    bool multiInstance = false;  // -multi
    std::wstring windowTitle;    // -title <name>  (empty = unset)

    std::string classicCdKey;  // -d2c <key>  (empty = unset)
    std::string lodCdKey;      // -d2x <key>  (empty = unset)

    bool reduceFailToJoin = false;    // -ftj
    bool randomizeBnetCache = false;  // -cachefix

    bool sleepy = false;  // -sleepy  (force the game loop to sleep even when focused)

    bool disableConsole = false;  // -noconsole

    std::optional<std::string> proxy;  // -proxy socks5://[user:password@]host:port

    std::vector<std::string> realms;  // -realm name:host (repeatable); raw specs, parsed into RealmRegistry

    bool disableAnalytics = false;  // -noanalytics
};

// Parse GetCommandLineW() once and return a stable reference. Safe to call
// from any thread after process start; the parser runs under std::call_once.
const LaunchOptions& GetLaunchOptions();

// The table GetLaunchOptions parses: every switch with its syntax and help text. A host that embeds the backend
// lists it (help, validation) and forwards the switches it was given on the command line the backend reads.
const std::vector<core::config::OptionSpec<LaunchOptions>>& GetLaunchOptionSpecs();

}  // namespace d2bs::game
