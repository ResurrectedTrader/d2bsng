#pragma once

#include <atomic>
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

namespace d2bs::framework::update {

// Background poller that checks the project's GitHub releases on a fixed
// interval and flags when a published release is newer than the running build
// (D2BS_VERSION). The game loop reads UpdateAvailable() on game entry to surface
// a one-line notice in-game. Pre-release / dev builds (a D2BS_VERSION carrying a
// -suffix) are not released versions and opt out entirely - they never start the
// poll. The HTTP request + JSON parse run on a dedicated jthread (never the game
// thread or a V8 isolate thread); UpdateAvailable() is a lock-free atomic read
// safe to call from any thread.
class UpdateChecker {
   public:
    static UpdateChecker& Instance();

    // Start the polling thread. Idempotent - a second call while running is a
    // no-op, as is a call on a pre-release / dev build (D2BS_VERSION with a
    // suffix), which opts out of checking. The first check runs after a short
    // settle delay, then every 6h.
    void Start();

    // Request the polling thread to stop and join it. Idempotent. May block
    // briefly (up to the in-flight request's timeout) if a check is mid-flight.
    void Stop();

    // Whether the most recent successful check found a release newer than this
    // build. Recomputed every poll (not a permanent latch), so it can clear
    // again if a later poll sees an equal/older release. Lock-free; safe to call
    // from the game thread.
    bool UpdateAvailable() const { return updateAvailable_.load(std::memory_order_acquire); }

    // The one-line ASCII notice to show in-game (e.g. "d2bsng 2.1.0 is
    // available (running 2.0.0)"). Empty when no update has been found. No URL
    // by design - the notice only signals that an update exists.
    std::string Message() const;

    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;
    UpdateChecker(UpdateChecker&&) = delete;
    UpdateChecker& operator=(UpdateChecker&&) = delete;

   private:
    UpdateChecker();
    ~UpdateChecker();

    // Polling loop body: an initial settle delay, then CheckOnce() every
    // interval. Both waits are interruptible via the stop_token.
    void Run(const std::stop_token& stopToken);

    // Perform one poll: GET the releases API, parse the latest tag, compare it
    // against D2BS_VERSION, and update updateAvailable_ / latestVersion_.
    // Returns true if the check completed (whether or not an update was found),
    // false on any network / parse failure.
    bool CheckOnce();

    inline static std::shared_ptr<spdlog::logger> logger_;

    mutable std::mutex mutex_;          // guards latestVersion_ + the cv wait
    std::condition_variable_any cv_;    // woken by the stop_token on Stop()
    std::atomic<bool> started_{false};  // Start() latch (idempotency)
    std::atomic<bool> updateAvailable_{false};
    std::string latestVersion_;  // newest tag observed (no leading 'v'); guarded by mutex_
    // Declared last so it is destroyed first - ~jthread requests stop + joins
    // while mutex_/cv_ are still alive for the loop's final wakeup.
    std::jthread thread_;
};

}  // namespace d2bs::framework::update
