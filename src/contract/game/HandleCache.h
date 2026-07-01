#pragma once

#include <atomic>
#include <cstdint>

namespace d2bs::game {

// Global frame generation counter. Incremented by the game loop hook on each new frame.
// Script thread reads this atomically to check if cached pointers are still valid.
inline std::atomic<uint64_t> frameGeneration = 0;
inline void InvalidateHandles() {
    frameGeneration.fetch_add(1, std::memory_order_release);
}

// Per-handle pointer cache. Shared by all identity-based handle types.
// Caches the resolved game pointer for the current frame to avoid repeated
// linked-list traversals when multiple properties are accessed on the same handle.
//
// TOCTOU safety: The check-and-use sequence (Get() -> use pointer) is subject to
// time-of-check-time-of-use races with the game thread advancing frames. The
// GameReadLock acquired inside ResolvePtr() must be held for the entire
// resolve+use sequence, not just the resolution. With the lock held, the game
// thread cannot call InvalidateHandles() or modify game data, making the cached
// pointer safe to use.
struct HandleCache {
    mutable void* ptr = nullptr;
    mutable uint64_t gen = 0;

    void* Get() const {
        if (ptr != nullptr && gen == frameGeneration.load(std::memory_order_acquire)) {
            return ptr;
        }
        return nullptr;
    }

    void Set(void* p) const {
        ptr = p;
        gen = frameGeneration.load(std::memory_order_acquire);
    }
};

}  // namespace d2bs::game
