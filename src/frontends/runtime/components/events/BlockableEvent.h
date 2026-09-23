#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include "BaseEvent.h"
#include "components/gameloop/GameLoop.h"
#include "game/GameLock.h"

// Flip to 1 to make every BlockableEvent fire-and-forget: scripts still
// receive the event, but the dispatching thread (game / network) doesn't
// wait for them to vote. Trade-off - scripts lose the ability to suppress
// packets / chat / key events, but the dispatcher never stalls.
#define D2BSNG_BLOCKABLE_NO_WAIT 1

namespace d2bs {

class BlockableEvent : public BaseEvent {
    std::promise<bool> promise_;
    std::shared_future<bool> future_ = promise_.get_future();
    std::once_flag promiseOnce_;
    std::atomic<int32_t> remaining_{0};

    void ResolvePromise(bool blocked) {
        std::call_once(promiseOnce_, [this, blocked]() { promise_.set_value(blocked); });
    }

   protected:
    BlockableEvent() = default;

    // Records this script's answer and retires it from the expected set.
    void Vote(bool blocked) {
        if (blocked) {
            ResolvePromise(true);
        }
        // Always decrement remaining count - both block=true and block=false paths.
        // If this was the last script and nobody blocked, resolve as not-blocked.
        // call_once prevents double-resolve if block=true already resolved above.
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ResolvePromise(false);
        }
    }

   public:
    // Called before dispatching to each script that will receive this event.
    // Must be called before any Execute() can run (i.e., during the dispatch loop).
    void IncrementExpected() { remaining_.fetch_add(1, std::memory_order_release); }

    // Compensates a prior IncrementExpected() when the dispatch was rejected
    // (e.g., PostEvent fails because the script is transitioning to Stopping).
    // If this was the last outstanding script, resolves the promise as not blocked.
    void DecrementExpected() {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ResolvePromise(false);
        }
    }

    // Called by FireIfRunning after the dispatch loop if no scripts were dispatched to.
    // Resolves the promise to false so the caller does not block forever.
    void ResolveIfNoneExpected() {
        if (remaining_.load(std::memory_order_acquire) == 0) {
            ResolvePromise(false);
        }
    }

    void OnDropped() override { DecrementExpected(); }

    void Execute(runtime::script::Invocation& call) override { Vote(call.Run(*this)); }

    [[nodiscard]] std::optional<bool> IsBlocked(
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) const {
#if D2BSNG_BLOCKABLE_NO_WAIT
        // Fire-and-forget: scripts still receive the event via the dispatch
        // loop, but we don't wait for them. Caller treats nullopt as "not
        // blocked" via .value_or(false), so packets / chat / keys flow
        // through immediately.
        (void)timeout;
        return std::nullopt;
#else
        // Release GameWriteLock (if held by game thread) so scripts can acquire
        // GameReadLock to process the event handler. Re-acquires on scope exit.
        // No-op when no write lock is held.
        const auto phase = runtime::gameloop::GameLoop::Instance().InPhase(runtime::gameloop::FramePhase::ScriptWait);
        game::GameWriteLockReleaser releaser;
        if (future_.wait_for(timeout) == std::future_status::timeout) {
            return std::nullopt;
        }
        return future_.get();
#endif
    }
};

}  // namespace d2bs
