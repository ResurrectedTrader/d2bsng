#include "config/CompatibilityFlags.h"

#include <algorithm>
#include <cstdint>

namespace d2bs::core::config {

namespace {

// Live BypassScopes on this thread.
thread_local uint32_t bypassDepth = 0;

}  // namespace

CompatibilityFlags::BypassScope::BypassScope() {
    ++bypassDepth;
}

CompatibilityFlags::BypassScope::~BypassScope() {
    --bypassDepth;
}

CompatibilityFlags& CompatibilityFlags::Instance() {
    static CompatibilityFlags instance;
    return instance;
}

void CompatibilityFlags::Register(const game::CompatibilityFlag& flag) {
    std::scoped_lock lock(mutex_);
    for (const auto& existing : flags_) {
        if (existing.name == flag.name) {
            return;  // first registration wins
        }
    }
    flags_.push_back(Flag{.name = flag.name, .defaultEnabled = flag.defaultEnabled, .enabled = flag.defaultEnabled});
}

void CompatibilityFlags::Register(std::string_view name, bool defaultEnabled) {
    Register(game::CompatibilityFlag{.name = std::string(name), .defaultEnabled = defaultEnabled});
}

bool CompatibilityFlags::IsEnabled(std::string_view name) const {
    if (bypassDepth != 0) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    for (const auto& flag : flags_) {
        if (flag.name == name) {
            return flag.enabled;
        }
    }
    return false;
}

bool CompatibilityFlags::Has(std::string_view name) const {
    std::scoped_lock lock(mutex_);
    return std::ranges::any_of(flags_, [&](const Flag& flag) { return flag.name == name; });
}

bool CompatibilityFlags::SetEnabled(std::string_view name, bool enabled) {
    std::scoped_lock lock(mutex_);
    for (auto& flag : flags_) {
        if (flag.name == name) {
            flag.enabled = enabled;
            return true;
        }
    }
    return false;
}

void CompatibilityFlags::Reset() {
    std::scoped_lock lock(mutex_);
    for (auto& flag : flags_) {
        flag.enabled = flag.defaultEnabled;
    }
}

std::vector<CompatibilityFlags::Flag> CompatibilityFlags::All() const {
    std::scoped_lock lock(mutex_);
    return flags_;
}

/// A scripting compatibility flag: a named toggle for a SpiderMonkey / kolbot-era behavior, as `Compatibility.set`
/// takes it. Every flag starts enabled.
void CompatibilityFlags::RegisterDefaults() {
    // The framework's built-in flags. Each /// @description is the catalog text
    // shown in the API docs - scripts/extract_api.py reads these Register() calls
    // (the same way it reads RegisterConstants). The flag names must match the
    // gating sites in CompileSource (the prelude features / jsStrictShim /
    // constRunnableRewrite) and JSProfile (profileCallWithoutNew).

    /// @description String.prototype.contains / Array.prototype.contains aliases (SpiderMonkey extension; the standard
    /// name is includes()).
    Register("stringContains");

    /// @description SpiderMonkey-style Error stack traces: a 'func@file:line:col' Error.prepareStackTrace formatter
    /// plus a raised Error.stackTraceLimit, which require.js / LazyLoader parse to resolve relative imports.
    Register("errorStackTrace");

    /// @description Non-standard Error.prototype fileName / lineNumber / columnNumber accessors, parsed out of the
    /// stack.
    Register("errorSpiderMonkeyProps");

    /// @description Object.prototype.toSource(): an eval-roundtrippable representation V8 dropped; kolbot error-logging
    /// paths call it.
    Register("objectToSource");

    /// @description Treat a 'js_strict(true);' line in a script as a request to run it in strict mode (prepends "use
    /// strict";).
    Register("jsStrictShim");

    /// @description Rewrite 'const X = new Runnable' to 'var X = new Runnable' so the binding lands on the global
    /// object for cross-script lookup.
    Register("constRunnableRewrite");

    /// @description Allow calling Profile(...) without 'new' (SpiderMonkey permitted it; kolbot relies on it).
    Register("profileCallWithoutNew");

    /// @description getSkillByName and unit.setSkill(name) look names up in the 1.14d skill table (the localized
    /// names kolbot uses, e.g. "Phoenix Strike") rather than the game's skills.txt names (e.g. "Royal strike").
    Register("legacySkillNames");

    /// @description include() of a file that fails to compile or throws reports the error and returns false, as d2bs
    /// did; when disabled the error is reported and also rethrown into the caller.
    Register("includeReturnsFalse");
}

}  // namespace d2bs::core::config
