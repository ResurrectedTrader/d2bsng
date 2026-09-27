#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include "game/GameLock.h"
#include "game/HandleCache.h"

using d2bs::game::GameReadLock;
using d2bs::game::GameReadLockReleaser;
using d2bs::game::GameWriteLock;
using d2bs::game::Resolved;

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

TEST_CASE("Resolved resolves under the read lock and holds it for its lifetime") {
    REQUIRE_FALSE(GameReadLock::IsHeldByCurrentThread());
    int32_t value = 7;
    bool isHeldDuringResolve = false;
    {
        const Resolved<int32_t> resolved([&]() -> void* {
            isHeldDuringResolve = GameReadLock::IsHeldByCurrentThread();
            return &value;
        });
        CHECK(isHeldDuringResolve);
        CHECK(GameReadLock::IsHeldByCurrentThread());
        const int32_t* ptr = resolved;
        CHECK(ptr == &value);
        CHECK(*resolved == 7);
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

// A temporary must not yield its pointer: the lock would be gone by the time it is used.
static_assert(std::is_convertible_v<const Resolved<int32_t>&, int32_t*>);
static_assert(!std::is_convertible_v<Resolved<int32_t>&&, int32_t*>);
static_assert(!std::is_convertible_v<const Resolved<int32_t>&&, int32_t*>);
template <typename R>
concept Dereferenceable = requires { *std::declval<R>(); };
static_assert(Dereferenceable<const Resolved<int32_t>&>);
static_assert(!Dereferenceable<Resolved<int32_t>>);
static_assert(!Dereferenceable<const Resolved<int32_t>>);
// The resolver constructor must not stand in for the deleted copy constructor.
static_assert(!std::is_copy_constructible_v<Resolved<int32_t>>);
static_assert(std::is_move_constructible_v<Resolved<int32_t>>);

TEST_CASE("Resolved of a missing object is empty but still locked") {
    {
        const Resolved<int32_t> resolved([]() -> void* { return nullptr; });
        const int32_t* ptr = resolved;
        CHECK(ptr == nullptr);
        const bool isMissing = !resolved;
        CHECK(isMissing);
        CHECK(GameReadLock::IsHeldByCurrentThread());
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("Resolved nests re-entrantly") {
    int32_t value = 1;
    const auto resolve = [&]() -> void* {
        return &value;
    };
    {
        const GameReadLock outer;
        {
            const Resolved<int32_t> first(resolve);
            CHECK(GameReadLock::RecursionDepth() == 2);
            {
                const Resolved<int32_t> second(resolve);
                CHECK(GameReadLock::RecursionDepth() == 3);
                CHECK(static_cast<int32_t*>(second) == static_cast<int32_t*>(first));
            }
            CHECK(GameReadLock::RecursionDepth() == 2);
        }
        CHECK(GameReadLock::RecursionDepth() == 1);
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("Moving a Resolved hands its lock over") {
    int32_t value = 3;
    std::optional<Resolved<int32_t>> holder;
    {
        Resolved<int32_t> source([&]() -> void* { return &value; });
        holder.emplace(std::move(source));
        CHECK(GameReadLock::RecursionDepth() == 1);
    }
    CHECK(GameReadLock::IsHeldByCurrentThread());
    REQUIRE(holder.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
    CHECK(static_cast<int32_t*>(*holder) == &value);
    // NOLINTEND(bugprone-unchecked-optional-access)
    holder.reset();
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("Release epoch moves only when a held read lock is released") {
    const auto idle = GameReadLock::ReleaseEpoch();
    { const GameReadLockReleaser releaser; }
    CHECK(GameReadLock::ReleaseEpoch() == idle);

    int32_t value = 5;
    const Resolved<int32_t> resolved([&]() -> void* { return &value; });
    const auto held = GameReadLock::ReleaseEpoch();
    {
        const GameReadLockReleaser releaser;
        CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
    }
    CHECK(GameReadLock::IsHeldByCurrentThread());
    CHECK(GameReadLock::ReleaseEpoch() == held + 1);
}

TEST_CASE("Release epoch moves when the write lock is yielded") {
    const GameWriteLock writer;
    const auto held = GameReadLock::ReleaseEpoch();
    { const d2bs::game::GameWriteLockReleaser releaser; }
    CHECK(GameReadLock::ReleaseEpoch() == held + 1);
}

TEST_CASE("IsGameLockHeld covers both lock kinds") {
    CHECK_FALSE(d2bs::game::IsGameLockHeld());
    {
        const GameReadLock reader;
        CHECK(d2bs::game::IsGameLockHeld());
    }
    {
        const GameWriteLock writer;
        CHECK(d2bs::game::IsGameLockHeld());
    }
    CHECK_FALSE(d2bs::game::IsGameLockHeld());
}
