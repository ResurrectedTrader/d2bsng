#pragma once

#include <atomic>
#include <cstdint>

namespace d2bs::runtime::events {

// Rendered frames since load. The game thread counts each frame; each script fires its "render"
// listeners from its own event loop when the count has moved past the frame it last fired for, so
// nothing is queued per frame and a script that falls behind just sees a bigger jump.
class FrameCounter {
    inline static std::atomic<uint64_t> count_ = 0;

   public:
    // Game thread, once per rendered frame.
    static void Increment() { count_.fetch_add(1, std::memory_order_relaxed); }

    // Any thread. The number of the most recently rendered frame (the first is 1).
    static uint64_t Current() { return count_.load(std::memory_order_relaxed); }
};

}  // namespace d2bs::runtime::events
