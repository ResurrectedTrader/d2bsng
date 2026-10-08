#pragma once

#include <atomic>
#include <cstdint>

namespace d2bs::runtime::events {

// The per-frame "render" event is a signal, not a queued event: the game thread only counts rendered
// frames, and each script's event loop fires its render listeners when the count has moved since it
// last did. Nothing is queued per frame, so a script that falls behind just sees a bigger jump. Kept
// free of V8 so the coalescing can be tested on its own.
class RenderFrames {
    inline static std::atomic<uint32_t> count_ = 0;

   public:
    // Game thread, once per rendered frame.
    static void Count() { count_.fetch_add(1, std::memory_order_relaxed); }

    // Any thread. The number of the most recently rendered frame (the first is 1).
    static uint32_t Latest() { return count_.load(std::memory_order_relaxed); }

    // True if a frame was rendered since `seen`, which then moves to the latest frame.
    static bool TakeNew(uint32_t& seen) {
        const uint32_t latest = Latest();
        if (latest == seen) {
            return false;
        }
        seen = latest;
        return true;
    }
};

}  // namespace d2bs::runtime::events
