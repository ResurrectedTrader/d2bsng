#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <shared_mutex>

namespace d2bs::game {

// Recursive shared (read) lock for script threads.
// First acquisition per thread takes the real shared_lock (~30ns).
// Nested acquisitions (e.g., Room::ResolvePtr -> Level::ResolvePtr) just
// increment a thread-local counter (~1ns).
// Multiple script threads hold shared locks concurrently - scripts don't block each other.
// When the current thread already holds the GameWriteLock, construction is a
// no-op: the writer has exclusive access on this thread, so a shared_lock on
// the same mutex would self-deadlock.
class GameReadLock {
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - thread-local by design
    inline static std::shared_mutex mutex_;
    inline static thread_local int32_t depth_ = 0;
    inline static thread_local std::shared_lock<std::shared_mutex> lock_;
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

    friend class GameWriteLock;
    friend class GameWriteLockReleaser;
    friend class GameReadLockReleaser;

   public:
    GameReadLock();
    ~GameReadLock() noexcept;

    GameReadLock(const GameReadLock&) = delete;
    GameReadLock& operator=(const GameReadLock&) = delete;
    GameReadLock(GameReadLock&&) = delete;
    GameReadLock& operator=(GameReadLock&&) = delete;

    // Diagnostic helpers - mirror GameWriteLock::IsHeldByCurrentThread so
    // hang-investigation code can log who is holding what at the moment of
    // interest. RecursionDepth lets callers distinguish "outer scope holds"
    // from "deeply nested" cases (common when a V8 callback enters under
    // Bridge::Lock and then resolves several handles via ResolvePtr).
    static bool IsHeldByCurrentThread() { return depth_ > 0; }
    static int32_t RecursionDepth() { return depth_; }
};

// Exclusive (write) lock for the game thread.
// Blocks until all GameReadLocks are released.
//
// Held by the game thread across the whole game-frame body (GameLoop::OnSleep)
// and released only around the real ::Sleep call so script readers can acquire
// GameReadLock. The exclusive window coincides with the window where the game
// thread is already the sole writer of game memory, so there is no contention
// against the game thread itself - and scripts see a natural low-latency read
// window during ::Sleep.
//
// Ownership model: the currently-active instance on a given thread is tracked via
// a thread_local pointer so GameWriteLockReleaser can yield through the owned
// unique_lock (keeping its owns_lock() state consistent) instead of touching the
// raw mutex. Non-reentrant.
class GameWriteLock {
    std::unique_lock<std::shared_mutex> lock_;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - thread-local by design
    inline static thread_local GameWriteLock* active_ = nullptr;
    // Holder for Acquire()/Release() - the framework's Sleep-hook lock
    // lifecycle is not stack-scoped (release before ::Sleep, reacquire on
    // wake), so we store the instance on the thread instead of the stack.
    // Definition is out-of-class so std::unique_ptr's destructor sees the
    // complete GameWriteLock type.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - thread-local by design
    static thread_local std::unique_ptr<GameWriteLock> manual_;

    friend class GameWriteLockReleaser;
    friend class GameReadLock;

   public:
    GameWriteLock() : lock_(GameReadLock::mutex_) {
        assert(active_ == nullptr && "GameWriteLock is not reentrant");
        active_ = this;
    }
    ~GameWriteLock() { active_ = nullptr; }

    GameWriteLock(const GameWriteLock&) = delete;
    GameWriteLock& operator=(const GameWriteLock&) = delete;
    GameWriteLock(GameWriteLock&&) = delete;
    GameWriteLock& operator=(GameWriteLock&&) = delete;

    // Manual acquire/release for the framework's Sleep-hook lock lifecycle.
    // Distinct from the RAII ctor/dtor path - release and reacquire happen at
    // non-stack-scoped points (before/after the ::Sleep call).
    static void Acquire();
    static void Release();
    static bool IsHeldByCurrentThread() { return active_ != nullptr; }
};

inline thread_local std::unique_ptr<GameWriteLock> GameWriteLock::manual_;

inline void GameWriteLock::Acquire() {
    assert(active_ == nullptr && "GameWriteLock::Acquire while lock already held");
    manual_ = std::make_unique<GameWriteLock>();
}

inline void GameWriteLock::Release() {
    assert(active_ != nullptr && "GameWriteLock::Release with no lock held");
    manual_.reset();
}

inline GameReadLock::GameReadLock() {
    if (GameWriteLock::active_ != nullptr) {
        return;
    }
    if (depth_ == 0) {
        lock_ = std::shared_lock(mutex_);
    }
    depth_++;
}

// NOLINTNEXTLINE(bugprone-exception-escape) - unlock() won't throw for shared_mutex on any platform
inline GameReadLock::~GameReadLock() noexcept {
    // Re-check writer state at destruction. The lock is stack-scoped and active_
    // is thread-local, so it can't change between ctor and dtor on the same thread.
    if (GameWriteLock::active_ != nullptr) {
        return;
    }
    if (--depth_ == 0) {
        lock_.unlock();
    }
}

// RAII releaser: if the current thread owns a GameWriteLock, temporarily releases
// it (via the owned unique_lock) and re-acquires on destruction. No-op if no
// write lock is held on this thread - which covers both "non-game thread calls
// IsBlocked()" and "game thread called IsBlocked() outside the frame-boundary
// window". Used inside BlockableEvent::IsBlocked() so scripts can acquire
// GameReadLock to run handlers while the game thread waits for a response.
//
// Lifetime invariant: a GameWriteLockReleaser must not outlive the GameWriteLock
// it released. This is guaranteed by the intended RAII-nested usage (releaser
// declared in a scope strictly inside the write lock's scope) and not enforced
// at runtime.
class GameWriteLockReleaser {
    GameWriteLock* released_ = nullptr;

   public:
    GameWriteLockReleaser() : released_(GameWriteLock::active_) {
        if (released_) {
            assert(released_->lock_.owns_lock() && "GameWriteLock in unexpected state at release");
            released_->lock_.unlock();
            // Null the thread-local so nested releasers observe "no lock held" and
            // correctly no-op instead of double-unlocking.
            GameWriteLock::active_ = nullptr;
        }
    }
    // NOLINTNEXTLINE(bugprone-exception-escape) - lock() won't throw for shared_mutex on any platform
    ~GameWriteLockReleaser() noexcept {
        if (released_) {
            assert(!released_->lock_.owns_lock() && "GameWriteLock in unexpected state at re-acquire");
            released_->lock_.lock();
            GameWriteLock::active_ = released_;
        }
    }

    GameWriteLockReleaser(const GameWriteLockReleaser&) = delete;
    GameWriteLockReleaser& operator=(const GameWriteLockReleaser&) = delete;
    GameWriteLockReleaser(GameWriteLockReleaser&&) = delete;
    GameWriteLockReleaser& operator=(GameWriteLockReleaser&&) = delete;
};

// RAII releaser: fully releases all recursive GameReadLock levels, re-acquires on destruction.
// Used when a script thread needs the game thread to proceed (e.g., GameThread::Execute).
// Saves the current recursion depth, releases the shared lock, and restores both on destruction.
class GameReadLockReleaser {
    int32_t savedDepth_;

   public:
    GameReadLockReleaser() : savedDepth_(GameReadLock::depth_) {
        if (savedDepth_ > 0) {
            GameReadLock::depth_ = 0;
            GameReadLock::lock_.unlock();
        }
    }

    // NOLINTNEXTLINE(bugprone-exception-escape) - shared_lock/unlock won't throw for shared_mutex
    ~GameReadLockReleaser() noexcept {
        if (savedDepth_ > 0) {
            GameReadLock::lock_ = std::shared_lock(GameReadLock::mutex_);
            GameReadLock::depth_ = savedDepth_;
        }
    }

    GameReadLockReleaser(const GameReadLockReleaser&) = delete;
    GameReadLockReleaser& operator=(const GameReadLockReleaser&) = delete;
    GameReadLockReleaser(GameReadLockReleaser&&) = delete;
    GameReadLockReleaser& operator=(GameReadLockReleaser&&) = delete;
};

}  // namespace d2bs::game
