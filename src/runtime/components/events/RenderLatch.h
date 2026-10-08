#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace d2bs::runtime::events {

// One script's share of the per-frame "render" event. The game thread queues at most one render
// event per script: a script that falls behind gets the next frame once it catches up, not a
// backlog of every frame it missed. Kept free of V8 so the coalescing can be tested on its own.
class RenderLatch {
    inline static std::atomic<uint32_t> frames_ = 0;

   public:
    // Game thread, once per rendered frame. Returns the new frame's number (the first is 1).
    static uint32_t CountFrame() { return frames_.fetch_add(1, std::memory_order_acq_rel) + 1; }

    // Any thread. The number of the most recently rendered frame.
    static uint32_t LastFrame() { return frames_.load(std::memory_order_acquire); }

    // Game thread. True if no render event was pending, and marks one pending.
    bool TryArm() { return !isPending_.exchange(true, std::memory_order_acq_rel); }

    // Game thread. Undoes a TryArm whose event never reached the script's queue.
    void Disarm() { isPending_.store(false, std::memory_order_release); }

    // Script thread, as the event runs. Clears the pending mark before the handlers run, so a frame
    // drawn meanwhile queues the next event, and returns the milliseconds since the previous delivery
    // (0 for the first).
    uint32_t Deliver(std::chrono::steady_clock::time_point now) {
        Disarm();
        const auto previous = std::exchange(lastDelivery_, now);
        if (!previous.has_value() || now < *previous) {
            return 0;
        }
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - *previous).count());
    }

   private:
    std::atomic<bool> isPending_ = false;
    // Script-thread only.
    std::optional<std::chrono::steady_clock::time_point> lastDelivery_;
};

}  // namespace d2bs::runtime::events
