#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "game/Compatibility.h"

namespace d2bs::config {

// Global, thread-shared registry of scripting compatibility flags. Every flag
// is a named toggle for a SpiderMonkey/kolbot-era behavior; all default to
// enabled. The framework registers its built-in flags via RegisterDefaults();
// game-version ports contribute their own via Register(). Scripts read and
// toggle them through the `Compatibility` JS object. See docs/compatibility.md.
//
// One instance (Instance()), guarded by a mutex. Flags are registered once at
// framework init (single-threaded, before scripts run); thereafter the set is
// fixed and only the enabled bits change, read-mostly with occasional writes
// from script threads.
class CompatibilityFlags {
   public:
    struct Flag {
        std::string name;
        bool defaultEnabled;
        bool enabled;
    };

    static CompatibilityFlags& Instance();

    // Register the framework's built-in flags. Idempotent.
    void RegisterDefaults();

    // Register one flag. Idempotent by name - a duplicate name keeps the first
    // registration (and its current enabled state). The string_view overload is
    // the convenient form used by RegisterDefaults.
    void Register(const game::CompatibilityFlag& flag);
    void Register(std::string_view name, bool defaultEnabled = true);

    // Whether the named flag is enabled. Unknown names return false.
    [[nodiscard]] bool IsEnabled(std::string_view name) const;

    // Whether the named flag exists in the registry.
    [[nodiscard]] bool Has(std::string_view name) const;

    // Enable/disable a flag. Returns false (and changes nothing) if unknown.
    bool SetEnabled(std::string_view name, bool enabled);

    // Restore every flag to its registered default.
    void Reset();

    // Snapshot of all flags, in registration order.
    [[nodiscard]] std::vector<Flag> All() const;

   private:
    CompatibilityFlags() = default;

    mutable std::mutex mutex_;
    std::vector<Flag> flags_;  // registration order preserved; small N, linear scan
};

}  // namespace d2bs::config
