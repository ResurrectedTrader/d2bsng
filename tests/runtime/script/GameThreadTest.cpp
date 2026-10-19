#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "game/GameLock.h"
#include "game/GameThread.h"

using namespace std::chrono_literals;

using d2bs::game::GameReadLock;
using d2bs::game::GameThread;
using d2bs::game::GameWriteLock;

TEST_CASE("GameThread Execute needs a drain even after the joining thread releases its game lock") {
    std::promise<void> entered;
    std::promise<int32_t> completed;
    auto enteredFuture = entered.get_future();
    auto completedFuture = completed.get_future();
    const auto gameThreadId = std::this_thread::get_id();
    std::thread::id executedOn;
    std::jthread script([&] {
        const GameReadLock reader;
        entered.set_value();
        completed.set_value(GameThread::Execute([&] {
            executedOn = std::this_thread::get_id();
            return 42;
        }));
    });
    enteredFuture.wait();
    {
        // Acquiring the writer proves Execute queued its work and released the reader.
        const GameWriteLock writer;
    }
    // This is the profile-switch join cycle: releasing the writer alone cannot
    // let the script finish, since this same thread must still drain its task.
    CHECK(completedFuture.wait_for(20ms) == std::future_status::timeout);
    {
        const GameWriteLock writer;
        GameThread::Drain();
    }
    script.join();
    CHECK(completedFuture.get() == 42);
    CHECK(executedOn == gameThreadId);
}

TEST_CASE("GameThread stop cancels a pending call and joins without draining") {
    std::promise<void> entered;
    std::promise<bool> completed;
    auto enteredFuture = entered.get_future();
    auto completedFuture = completed.get_future();
    std::atomic<bool> invoked = false;
    std::jthread script([&](const std::stop_token& token) {
        const GameThread::CancellationScope cancellation(token);
        const GameReadLock outer;
        const GameReadLock inner;
        int32_t referencedValue = 42;
        entered.set_value();
        try {
            GameThread::Execute([&] {
                invoked = true;
                return referencedValue;
            });
            completed.set_value(false);
        } catch (const GameThread::Cancelled&) {
            completed.set_value(GameReadLock::RecursionDepth() == 2);
        }
    });
    enteredFuture.wait();
    {
        // Proves Execute enqueued and released both recursive read locks.
        const GameWriteLock writer;
        script.request_stop();
        const d2bs::game::GameWriteLockReleaser releaser;
        const auto status = completedFuture.wait_for(2s);
        CHECK(status == std::future_status::ready);
        if (status != std::future_status::ready) {
            GameThread::Drain();  // Fail without hanging the suite if cancellation breaks.
        }
        script.join();
    }
    CHECK(completedFuture.get());
    {
        const GameWriteLock writer;
        GameThread::Drain();  // The referenced stack variable is gone now.
    }
    CHECK_FALSE(invoked.load());
}

TEST_CASE("GameThread already stopped calls fail before dispatch including void calls") {
    std::stop_source source;
    source.request_stop();
    const GameThread::CancellationScope cancellation(source.get_token());
    bool invoked = false;
    CHECK_THROWS_AS(GameThread::Execute([&] { invoked = true; }), GameThread::Cancelled);
    {
        const GameWriteLock writer;
        CHECK_THROWS_AS(GameThread::Execute([&] { invoked = true; }), GameThread::Cancelled);
    }
    CHECK_FALSE(invoked);
}

TEST_CASE("GameThread cancellation also skips calls in the current drain batch") {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<bool> cancelled = false;
    std::atomic<bool> invoked = false;
    std::stop_source source;
    GameThread::Post([&] { source.request_stop(); });
    std::jthread script([&] {
        const GameThread::CancellationScope cancellation(source.get_token());
        const GameReadLock reader;
        entered.set_value();
        try {
            GameThread::Execute([&] { invoked = true; });
        } catch (const GameThread::Cancelled&) {
            cancelled = true;
        }
    });
    enteredFuture.wait();
    {
        const GameWriteLock writer;
        GameThread::Drain();
    }
    script.join();
    CHECK(cancelled.load());
    CHECK_FALSE(invoked.load());
}

TEST_CASE("GameThread stop cannot release referenced data of an already running call") {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<bool> completed = false;
    std::jthread script([&](const std::stop_token& token) {
        const GameThread::CancellationScope cancellation(token);
        const GameReadLock reader;
        int32_t referencedValue = 42;
        entered.set_value();
        const auto result = GameThread::Execute([&] {
            script.request_stop();
            CHECK_FALSE(completed.load());
            return referencedValue;
        });
        completed = result == 42;
    });
    enteredFuture.wait();
    {
        const GameWriteLock writer;
        GameThread::Drain();
    }
    script.join();
    CHECK(completed.load());
}

TEST_CASE("GameThread forwards callable exceptions and drains subsequent tasks") {
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::atomic<bool> caught = false;
    bool posted = false;
    std::jthread script([&] {
        const GameReadLock reader;
        entered.set_value();
        try {
            GameThread::Execute([]() -> int32_t { throw std::runtime_error("task failed"); });
        } catch (const std::runtime_error&) {
            caught = true;
        }
    });
    enteredFuture.wait();
    {
        const GameWriteLock writer;
        GameThread::Post([&] { posted = true; });
        GameThread::Drain();
    }
    script.join();
    CHECK(caught.load());
    CHECK(posted);
}

TEST_CASE("GameThread cancellation scopes restore the enclosing token") {
    std::stop_source source;
    source.request_stop();
    const GameWriteLock writer;
    {
        const GameThread::CancellationScope outer(source.get_token());
        {
            const GameThread::CancellationScope inner(std::stop_token{});
            CHECK(GameThread::Execute([] { return 42; }) == 42);
        }
        CHECK_THROWS_AS(GameThread::Execute([] { return 42; }), GameThread::Cancelled);
    }
    CHECK(GameThread::Execute([] { return 42; }) == 42);
}
