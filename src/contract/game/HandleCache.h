#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "game/GameLock.h"

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
// TOCTOU safety: the check-and-use sequence (Get() -> use pointer) races the game
// thread advancing frames unless a GameReadLock is held across the whole
// resolve+use sequence, not just the resolution. With the lock held, the game
// thread cannot call InvalidateHandles() or modify game data, making the cached
// pointer safe to use. Resolved<T> below is how backends hold it.
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

// A game pointer resolved from a handle, together with the read lock that keeps it
// valid. The lock is taken before the resolve and held for this object's lifetime,
// so the resolve and every dereference through it see one consistent game state.
// Handles produce it with Resolve<T>(); T is the backend's own struct type.
//
// A wait that hands this thread's lock back (GameThread::Execute, PollUntil, anything
// built on GameReadLockReleaser) lets the game thread free or relink the structure,
// so the pointer must not be used after one: end the Resolved's scope before the wait
// and re-resolve after it. Debug builds assert this on every access; the check reads
// GameReadLock::ReleaseEpoch(), which only moves when a held lock was released.
//
// Bound to the thread that created it (the lock and the epoch are thread-local).
template <typename T>
class [[nodiscard]] Resolved {
    // Declared first: the lock is taken before ptr_ is resolved.
    std::optional<GameReadLock> lock_;
    T* ptr_;
#ifndef NDEBUG
    uint64_t epoch_ = GameReadLock::ReleaseEpoch();
#endif

    void AssertFresh() const {
        assert(epoch_ == GameReadLock::ReleaseEpoch() &&
               "resolved game pointer used after a wait released the game lock - re-resolve it");
    }

   public:
    // `resolve` returns the handle's raw pointer (void*) and runs under the lock.
    template <typename Resolve>
        requires std::is_invocable_r_v<void*, const Resolve&>
    explicit Resolved(const Resolve& resolve) : lock_(std::in_place), ptr_(static_cast<T*>(resolve())) {}

    // The destination takes its own (re-entrant, so free) lock before the source
    // drops its one, so the thread holds the lock throughout.
    Resolved(Resolved&& other) noexcept
        : lock_(std::in_place),
          ptr_(std::exchange(other.ptr_, nullptr))
#ifndef NDEBUG
          ,
          epoch_(other.epoch_)
#endif
    {
        other.lock_.reset();
    }

    Resolved(const Resolved&) = delete;
    Resolved& operator=(const Resolved&) = delete;
    Resolved& operator=(Resolved&&) = delete;
    ~Resolved() = default;

    // Converts only from a named Resolved: the pointer of a temporary would outlive
    // the lock that keeps it valid. Also serves the null test (`if (u)`, `!u`).
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions) - implicit by design
    operator T*() const& {
        AssertFresh();
        return ptr_;
    }
    operator T*() const&& = delete;

    T* operator->() const {
        AssertFresh();
        return ptr_;
    }

    T& operator*() const& {
        AssertFresh();
        return *ptr_;
    }
    T& operator*() const&& = delete;
};

}  // namespace d2bs::game
