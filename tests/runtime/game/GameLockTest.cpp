#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "game/GameLock.h"

using d2bs::game::GameReadLock;
using d2bs::game::GameWriteLock;

TEST_CASE("GameReadLock under GameWriteLock on same thread does not deadlock") {
    GameWriteLock writer;
    // If LOCK-1 were unresolved this would self-deadlock: GameReadLock would
    // call shared_lock on a mutex the current thread already owns exclusively.
    { GameReadLock reader; }
    // Writer destructs cleanly.
}

TEST_CASE("Nested GameReadLocks under GameWriteLock on same thread") {
    GameWriteLock writer;
    {
        GameReadLock reader1;
        {
            GameReadLock reader2;
            GameReadLock reader3;
        }
        GameReadLock reader4;
    }
}

TEST_CASE("GameReadLock works normally when no GameWriteLock is held") {
    // Sanity: the writer-subsumed path must not break the normal recursive case.
    GameReadLock outer;
    {
        GameReadLock inner;
        GameReadLock innermost;
    }
}

TEST_CASE("GameReadLock on another thread blocks while writer holds lock") {
    // Verify the writer-subsumed short-circuit is thread-local: a different
    // thread must still see a true shared_lock contending with the writer.
    GameWriteLock writer;
    std::atomic readerAcquired{false};
    std::thread t([&]() {
        GameReadLock reader;  // blocks until writer releases
        readerAcquired = true;
    });
    // Poll for a bounded window: while the writer holds the lock the reader
    // must never acquire. Polling (vs a single sleep + join) keeps the failure
    // mode clean - a misbehaving lock flips readerAcquired mid-window and we
    // see a CHECK failure, rather than hanging on join at the end.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
    while (std::chrono::steady_clock::now() < deadline) {
        CHECK_FALSE(readerAcquired.load());
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    CHECK_FALSE(readerAcquired.load());
    // Release writer via scoped releaser; reader should acquire shortly after.
    {
        d2bs::game::GameWriteLockReleaser release;
        for (int i = 0; i < 100 && !readerAcquired.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        t.join();
    }
    CHECK(readerAcquired.load());
}
