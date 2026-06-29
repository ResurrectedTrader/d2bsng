#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

// NOLINTBEGIN(readability-identifier-naming) - spdlog::logger is upstream API naming
namespace spdlog {
class logger;
}  // namespace spdlog
// NOLINTEND(readability-identifier-naming)

namespace d2bs::framework::update {

// A comparable major.minor.patch triple. 0.0.0 doubles as the "no update"
// sentinel - d2bsng releases start at 2.x.
struct SemVer {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    auto operator<=>(const SemVer&) const = default;
};

// Background poller that checks the project's GitHub releases on a fixed
// interval and flags when a published release is newer than the running build
// (D2BS_VERSION). The version banner reads AvailableUpdate() each frame to show
// the downloadable version. Pre-release / dev builds (a D2BS_VERSION carrying
// a -suffix) are not released versions and opt out entirely - they never start
// the poll. The HTTP request + JSON parse run on a dedicated jthread (never the
// game thread or a V8 isolate thread); AvailableUpdate() is safe to call from
// any thread, including the game thread.
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

    // The release found newer than this build, or nullopt when up to date.
    // Recomputed every poll. Safe to call from any thread, including the game
    // thread.
    std::optional<SemVer> AvailableUpdate() const {
        const SemVer v = availableVersion_.load(std::memory_order_acquire);
        if (v == SemVer{}) {
            return std::nullopt;
        }
        return v;
    }

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
    // against D2BS_VERSION, and update the updateAvailable_ flag. Returns true
    // if the check completed (whether or not an update was found), false on any
    // network / parse failure.
    bool CheckOnce();

    inline static std::shared_ptr<spdlog::logger> logger_;

    mutable std::mutex mutex_;          // guards the cv wait
    std::condition_variable_any cv_;    // woken by the stop_token on Stop()
    std::atomic<bool> started_{false};  // Start() latch (idempotency)
    std::atomic<SemVer> availableVersion_{};  // 0.0.0 = no update
    // Declared last so it is destroyed first - ~jthread requests stop + joins
    // while mutex_/cv_ are still alive for the loop's final wakeup.
    std::jthread thread_;
};

}  // namespace d2bs::framework::update
