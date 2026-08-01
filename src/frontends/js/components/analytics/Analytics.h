#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

// NOLINTBEGIN(readability-identifier-naming) - spdlog::logger is upstream API naming
namespace spdlog {
class logger;
}  // namespace spdlog
// NOLINTEND(readability-identifier-naming)

namespace d2bs::js::analytics {

// Fire-and-forget anonymous usage analytics. On startup it emits a
// "session_start" event to Aptabase describing the running build (d2bsng
// version, OS, locale, backend version, architecture) plus an anonymous install
// id and, when a bot manager supplies one, a user id for correlation. It then
// emits one "profile_active" event per distinct profile the launch runs, keyed
// by a per-install hash of the profile name (never the name itself), which is
// what makes "profiles per install" countable. Nothing is persisted: the install
// id is derived from salted machine facts, so analytics leaves no file or
// registry trace. The id derivation, JSON build, and HTTPS POST all run on a
// dedicated jthread - never the game thread or a V8 isolate.
//
// Analytics is off unless the build baked in an Aptabase app key (the
// D2BS_ANALYTICS_KEY compile-time macro, empty by default) and the opt-out
// (-noanalytics or D2BS_ANALYTICS_DISABLE) is absent. With no key it never
// starts a thread or touches the network. See docs/analytics.md.
class Analytics {
   public:
    static Analytics& Instance();

    // Resolve config and, if analytics is enabled, spawn the reporter thread.
    // Idempotent - a second call while running is a no-op, as is a call when
    // opted out or when no app key is configured. The event is sent after a
    // short settle delay.
    void Start();

    // Request the reporter thread to stop and join it. Idempotent. A POST in
    // flight is not cancellable, so this blocks until it finishes or hits its
    // 15s timeout.
    void Stop();

    Analytics(const Analytics&) = delete;
    Analytics& operator=(const Analytics&) = delete;
    Analytics(Analytics&&) = delete;
    Analytics& operator=(Analytics&&) = delete;

   private:
    Analytics();
    ~Analytics();

    // Reporter loop: an initial settle delay, SendStartupEvent() with a few
    // interruptible retries if the first POST fails (the network may not be up
    // yet at inject time), then WatchProfiles() for the rest of the launch.
    void Run(const std::stop_token& stopToken);

    // Poll the active profile name, reporting each distinct one exactly once.
    // Runs until Stop(): the name is only published when a script logs in or the
    // manager switches profiles, so there is nothing to observe up front and no
    // point at which more can be ruled out. Called with `lock` held; returns with
    // it held.
    void WatchProfiles(std::unique_lock<std::mutex>& lock, const std::stop_token& stopToken);

    // Interruptible sleep on the reporter thread. True => Stop() fired and the
    // caller should unwind.
    bool WaitFor(std::unique_lock<std::mutex>& lock, const std::stop_token& stopToken,
                 std::chrono::milliseconds duration);

    // Build and POST one event. Both return true on a 2xx response.
    bool SendStartupEvent();
    bool SendProfileEvent(const std::string& profileHash);

    inline static std::shared_ptr<spdlog::logger> logger_;

    mutable std::mutex mutex_;        // guards the cv wait
    std::condition_variable_any cv_;  // woken by the stop_token on Stop()
    std::atomic<bool> started_{false};

    // Resolved in Start() (the framework-init thread), then read-only on the
    // reporter thread - the thread spawn provides the happens-before edge.
    std::string userId_;  // optional bot-manager user id (empty = omit)
    std::string host_;    // ingest base URL derived from the key's region

    // Derived once on the reporter thread after the settle delay (both are
    // needed by every event), then only read there.
    std::string installId_;  // stable anonymous per-install id
    std::string sessionId_;  // per-process id tying this launch's events together

    // Declared last so it is destroyed first - ~jthread requests stop + joins
    // while mutex_/cv_ are still alive for the loop's final wakeup.
    std::jthread thread_;
};

}  // namespace d2bs::js::analytics
