#pragma once

#include <string>
#include <vector>

namespace d2bs::game {

// A compatibility-flag definition contributed by a game-version port. Flags are
// script-visible toggles for backwards-compatibility behaviors; see
// docs/compatibility.md.
struct CompatibilityFlag {
    std::string name;  // stable JS-visible key (camelCase)
    bool defaultEnabled = true;
};

// Compatibility flags the game-version port wants to expose to scripts. The
// framework registers these alongside its own built-in flags during init; once
// registered they are enabled/disabled at runtime through the unified
// `Compatibility` JS object, like any framework flag. Called once, before any
// script runs. A port with no version-specific flags returns an empty vector
// (1.14d does).
std::vector<CompatibilityFlag> GetCompatibilityFlags();

}  // namespace d2bs::game
