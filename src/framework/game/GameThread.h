#pragma once

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <source_location>
#include <type_traits>

#include "game/GameLock.h"
#include "utils/threadutils.h"

namespace d2bs::game {

// Executes functions on the game thread from script threads.
// Script threads post tasks to a queue; the GameLoop orchestrator drains the queue
// each frame on the game thread, outside the write lock. Use for game functions
// that depend on thread-local storage or must be called from the game thread
// (UI control manipulation, certain D2 API calls).
//
// Execute() is safe to call from the game thread - runs in-place.
class GameThread {
    struct QueuedTask {
        std::function<void()> func;
        std::source_location loc;
    };

    // Slow-task threshold. Tasks running longer than this on the game thread
    // log a warning naming the call site - the dominant cause of "the bot
    // stalls" is a single GameThread::Execute taking forever, and pinning the
    // submitter beats trying to figure out which lambda Drain() is running.
    // 50ms is roughly 3 frames at 60fps - anything past that is a visible
    // hitch.
    static constexpr std::chrono::milliseconds SLOW_TASK_THRESHOLD{50};

    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp) - singleton queue by design;
    // default-constructed, no real throw risk
    inline static std::mutex queueMutex_;
    inline static std::queue<QueuedTask> queue_;
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)

   public:
    // Post func to the game thread, block until it returns the result. Releases GameReadLock while waiting.
    template <typename F>
    static auto Execute(F func, std::source_location loc = std::source_location::current()) -> decltype(func()) {
        using T = decltype(func());

        if (GameWriteLock::IsHeldByCurrentThread()) {
            if constexpr (std::is_void_v<T>) {
                func();
                return;
            } else {
                return func();
            }
        }

        std::promise<T> promise;
        auto future = promise.get_future();
        {
            std::lock_guard lock(queueMutex_);
            if constexpr (std::is_void_v<T>) {
                queue_.push({[f = std::move(func), &promise] {
                                 f();
                                 promise.set_value();
                             },
                             loc});
            } else {
                queue_.push({[f = std::move(func), &promise] { promise.set_value(f()); }, loc});
            }
        }

        // Release game read lock so the game thread's frame-advance write lock
        // can proceed. Re-acquires on scope exit.
        GameReadLockReleaser releaser;
        return future.get();
    }

    // Post a function to the game thread without waiting.
    // The function is captured by value (must be copyable or moveable).
    template <typename F>
    static void Post(F&& func, std::source_location loc = std::source_location::current()) {
        {
            std::lock_guard lock(queueMutex_);
            queue_.push({std::function<void()>(std::forward<F>(func)), loc});
        }
    }

    // Drain the task queue on the game thread each frame. GameReadLock is a no-op under GameWriteLock, so tasks can
    // resolve handles without self-deadlock.
    static void Drain() {
        std::queue<QueuedTask> tasks;
        {
            std::lock_guard lock(queueMutex_);
            std::swap(tasks, queue_);
        }
        while (!tasks.empty()) {
            const auto& task = tasks.front();
            const auto start = std::chrono::steady_clock::now();
            // VEH stack shows only the generic lambda wrapper; annotate with the
            // posting site so first-chance AVs name the JS binding that posted.
            d2bs::thread_utils::CrashContextScope scope(std::format(
                "GameThread task from {}:{} ({})", std::filesystem::path(task.loc.file_name()).filename().string(),
                task.loc.line(), task.loc.function_name()));
            task.func();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (elapsed >= SLOW_TASK_THRESHOLD) {
                spdlog::warn("[GameThread] slow task: {}ms from {}:{} ({})", elapsed.count(),
                             std::filesystem::path(task.loc.file_name()).filename().string(), task.loc.line(),
                             task.loc.function_name());
            }
            tasks.pop();
        }
    }
};

}  // namespace d2bs::game
