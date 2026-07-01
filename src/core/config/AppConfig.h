#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>

#include "config/ScriptPaths.h"

namespace d2bs::config {

class ConfigStore;

// V8 inspector listening-port bounds and default. The sign of
// AppConfig::inspectorPort encodes enabled (positive) / disabled (non-positive)
// to avoid a second flag; the magnitude is the port. 9229 is the Node.js
// inspector default and is in chrome://inspect's default "Discover network
// targets" list, so targets appear with no manual config.
constexpr int32_t MIN_INSPECTOR_PORT = 1024;
constexpr int32_t MAX_INSPECTOR_PORT = 65535;
constexpr int32_t DEFAULT_INSPECTOR_PORT = 9229;

// Application bot settings and resolved paths. Scalar settings are std::atomic; path/string fields are set once before
// scripts start.
struct AppConfig {
    // Constructor/destructor defined in AppConfig.cpp where ConfigStore is complete
    // (unique_ptr<ConfigStore> requires a complete type for destruction).
    AppConfig();
    ~AppConfig();

    // Set once at init; no synchronization needed after that.
    std::unique_ptr<ConfigStore> store;

    // Bot settings (read/write from scripts, thread-safe via atomic).
    std::atomic<int32_t> chickenHp = 0;
    std::atomic<int32_t> chickenMp = 0;
    std::atomic<bool> quitOnHostile = false;
    std::atomic<bool> blockKeys = false;
    std::atomic<bool> blockMouse = false;
    std::atomic<bool> quitOnError = false;
    static_assert(std::atomic<std::chrono::milliseconds>::is_always_lock_free,
                  "std::atomic<chrono::milliseconds> must be lock-free on the target platform");
    std::atomic<std::chrono::milliseconds> maxGameTime{std::chrono::milliseconds{0}};
    std::atomic<bool> enableUnsupported = false;

    // V8 inspector (Chrome DevTools) debugging. Every script isolate always
    // registers a debuggable target; this controls whether the localhost
    // HTTP+WebSocket server that exposes them is running. The sign encodes
    // enabled/disabled (positive = server listening on that port, non-positive =
    // stopped, magnitude remembers the last port) so no second flag is needed.
    // Disabled by default (0); opt-in since it opens a local debug port.
    std::atomic<int32_t> inspectorPort = 0;

    // When true at startup, GameLoop suspends the script lifecycle until a
    // profile::Switch call clears the latch. Mirrors reference's UseProfileScript
    // setting - lets a launcher defer starter selection until it pokes the right
    // profile in via DDE. Atomic because script threads may read it concurrently.
    std::atomic<bool> waitForProfile = false;

    std::atomic<bool> startAtMenu{true};

    // D2BotNG manager message-window handle - the WM_COPYDATA target for
    // engine-side IPC (character state). 0 until the manager registers
    // it: it sends a "Handle" WM_COPYDATA whose dwData is the HWND, which the
    // framework's onIPC handler stores here (and updates again on handover).
    std::atomic<uintptr_t> managerHandle{0};

    // Global time-scaling multiplier ("speedhack"). Written exclusively via
    // speedhack::SetSpeed so the speedhack module can re-anchor virtual time
    // before the published value changes - direct stores here are forbidden.
    // Default 1.0 = no scaling. Loaded from d2bs.ini [settings]/Speed.
    std::atomic<float> speed{1.0F};

    // Additional settings - set once during initialization from INI [settings] section.
    // Read-only after init - no synchronization provided.
    std::chrono::milliseconds gameReadyTimeout{5000};  // for WaitForGameReady (INI value is seconds)
    size_t memoryLimit = 100 * 1024 * 1024;            // bytes, V8 heap limit (INI value is MB)

    // Extra V8 flags (INI [settings]/V8Flags) applied via SetFlagsFromString at
    // engine init. Read once in V8Host's constructor.
    std::string v8Flags;

    // V8 default-platform worker pool size (INI [settings]/V8ThreadPoolSize, 0 =
    // auto from CPU count, clamped [0, 64]). Ignored when v8SingleThreadedPlatform.
    int32_t v8ThreadPoolSize = 0;

    // Use V8's single-threaded platform (no worker pool) instead of the default
    // (INI [settings]/V8SingleThreadedPlatform). Forces the --single-threaded V8
    // flag that platform requires.
    bool v8SingleThreadedPlatform = false;

    // Real-wall granularity (ms) of the idle script / game-loop wait loops (INI
    // [settings]/IdleSleepIntervalMs, clamped [1, 100]). Larger = lower idle CPU,
    // coarser servicing.
    std::chrono::milliseconds idleSleepInterval{10};

    // Active profile name (set when login() is called, read by me.profile).
    // Names are normalized to lowercase on Set (matches reference's
    // GetPrivateProfileStringW case-insensitive section lookup) so internal
    // comparisons (snapshot diff, equality checks) are well-defined.
    std::string GetProfileName() const;
    void SetProfileName(std::string name);

    // Resolved runtime script paths. basePath is absolute (install dir joined
    // with the INI's [settings]/ScriptPath) and the three script names default
    // from [settings]. GameLoop overwrites these on profile switch by merging
    // the profile's ScriptPaths overrides. Guarded by stateMutex_.
    ScriptPaths GetScriptPaths() const;
    void SetScriptPaths(ScriptPaths paths);

    // Settings-level baseline - the ScriptPaths loaded from [settings] before
    // any profile override is layered on. Used by profile-switch reload so
    // per-profile overrides are applied against the immutable baseline rather
    // than the previously-resolved profile's paths (otherwise overrides from
    // profile foo would stick when switching foo -> bar where bar doesn't set
    // that key). Intended to be set only at init by IniConfigStore, but
    // synchronized through stateMutex_ to protect against future mistakes.
    ScriptPaths GetDefaultScriptPaths() const;
    void SetDefaultScriptPaths(ScriptPaths paths);

   private:
    std::string profileName_;
    ScriptPaths scriptPaths_;
    ScriptPaths defaultScriptPaths_;

    // Guards ScriptPaths + profileName + future per-profile mutable state.
    mutable std::shared_mutex stateMutex_;
};

// Process-wide singleton. Matches reference's global Vars struct.
AppConfig& GetAppConfig();

// Path sandboxing utilities.
// All script file access must be relative to the script base directory
// (stored in AppConfig::scriptPaths_.basePath).

// Returns true if relativePath is valid: no traversal (../), no invalid chars, stays under script base.
bool IsValidPath(const std::string& relativePath);

// Resolve relative path under the script base. Returns empty on validation failure.
std::filesystem::path GetPathRelScript(const std::string& relativePath);

}  // namespace d2bs::config
