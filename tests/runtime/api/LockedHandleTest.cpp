#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>
#include <utility>

#include "api/core/LockedHandle.h"
#include "components/navigation/ExitFinder.h"
#include "game/Control.h"
#include "game/GameLock.h"
#include "game/Level.h"
#include "game/Party.h"
#include "game/Room.h"
#include "game/StashTab.h"
#include "game/Types.h"
#include "game/Unit.h"

using d2bs::game::GameReadLock;
using d2bs::game::GameWriteLock;
using d2bs::runtime::api::GameHandle;
using d2bs::runtime::api::LockedHandle;

namespace game = d2bs::game;

// The natives ClassBase::Unwrap hands out under a lock: identity handles into live game memory.
static_assert(GameHandle<game::Unit>);
static_assert(GameHandle<game::Room>);
static_assert(GameHandle<game::Level>);
static_assert(GameHandle<game::Party>);
static_assert(GameHandle<game::Control>);
static_assert(GameHandle<game::StashTab>);
// Plain values copied out of game memory, and anything that is not game data, stay raw pointers.
static_assert(!GameHandle<game::PresetUnitInfo>);
static_assert(!GameHandle<d2bs::runtime::navigation::ExitInfo>);
static_assert(!GameHandle<int32_t>);

static_assert(!std::is_copy_constructible_v<LockedHandle<game::Unit>>);
static_assert(std::is_move_constructible_v<LockedHandle<game::Unit>>);
static_assert(!std::is_move_assignable_v<LockedHandle<game::Unit>>);
// The validity of the wrapper is only ever tested explicitly (`if (!data)`), never converted away.
static_assert(!std::is_convertible_v<LockedHandle<game::Unit>, bool>);

TEST_CASE("LockedHandle holds the read lock for its lifetime") {
    REQUIRE_FALSE(GameReadLock::IsHeldByCurrentThread());
    game::Unit unit;
    {
        const LockedHandle<game::Unit> data(&unit);
        CHECK(GameReadLock::IsHeldByCurrentThread());
        CHECK(static_cast<bool>(data));
        CHECK(&*data == &unit);
        CHECK(data.operator->() == &unit);
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("LockedHandle of a foreign receiver is empty but still locked") {
    {
        const LockedHandle<game::Unit> data(nullptr);
        CHECK_FALSE(static_cast<bool>(data));
        CHECK(GameReadLock::IsHeldByCurrentThread());
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("LockedHandle keeps the lock held across a move") {
    game::Unit unit;
    {
        LockedHandle<game::Unit> source(&unit);
        const LockedHandle<game::Unit> moved(std::move(source));
        CHECK(GameReadLock::IsHeldByCurrentThread());
        CHECK(GameReadLock::RecursionDepth() == 1);
        CHECK(&*moved == &unit);
        // NOLINTNEXTLINE(bugprone-use-after-move, hicpp-invalid-access-moved) - the moved-from state is under test
        CHECK_FALSE(static_cast<bool>(source));
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("LockedHandle nests re-entrantly with the accessors' own locks") {
    game::Unit unit;
    {
        const LockedHandle<game::Unit> data(&unit);
        {
            const GameReadLock accessor;
            CHECK(GameReadLock::RecursionDepth() == 2);
        }
        CHECK(GameReadLock::RecursionDepth() == 1);
    }
    CHECK_FALSE(GameReadLock::IsHeldByCurrentThread());
}

TEST_CASE("LockedHandle keeps the game thread's write lock out until it is released") {
    game::Unit unit;
    std::atomic isWriterIn{false};
    std::thread gameThread;
    {
        const LockedHandle<game::Unit> data(&unit);
        gameThread = std::thread([&] {
            const GameWriteLock writer;
            isWriterIn = true;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
        while (std::chrono::steady_clock::now() < deadline) {
            CHECK_FALSE(isWriterIn.load());
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    gameThread.join();
    CHECK(isWriterIn.load());
}
