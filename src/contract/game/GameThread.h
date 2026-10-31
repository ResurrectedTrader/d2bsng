#pragma once

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <source_location>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>

#include "game/GameLock.h"
#include "host/Host.h"
#include "utils/Profiling.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

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

    static spdlog::logger& Log() {
        static const auto LOGGER = utils::GetLogger("game.thread");
        return *LOGGER;
    }

    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp) - singleton queue by design;
    // default-constructed, no real throw risk
    inline static std::mutex queueMutex_;
    inline static std::queue<QueuedTask> queue_;
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)

    // The callable stays on the waiting thread's stack. Cancelled tasks must
    // never dereference it, even if Drain already took its batch from queue_.
    // The outcome (result, exception) is written before the release store of Done and read after the waiter's acquire.
    template <typename T>
    struct CallState {
        std::atomic<host::TaskState> state{host::TaskState::Pending};
        std::exception_ptr error;
        std::conditional_t<std::is_void_v<T>, std::monostate, std::optional<T>> result;

        bool Start() {
            auto expected = host::TaskState::Pending;
            return state.compare_exchange_strong(expected, host::TaskState::Running, std::memory_order_acq_rel);
        }

        void Cancel() {
            auto expected = host::TaskState::Pending;
            if (state.compare_exchange_strong(expected, host::TaskState::Cancelled, std::memory_order_acq_rel)) {
                state.notify_one();
            }
        }

        void Finish() {
            state.store(host::TaskState::Done, std::memory_order_release);
            state.notify_one();
        }
    };

    // Keep non-trivial TLS destruction out of header COMDATs (LLVM LTO).
    inline static thread_local const std::stop_token* stopToken_ = nullptr;

   public:
    struct Cancelled {};

    // Install the owning script's token for synchronous calls on this thread.
    class CancellationScope {
        std::stop_token token_;
        const std::stop_token* previous_;

       public:
        explicit CancellationScope(std::stop_token token)
            : token_(std::move(token)), previous_(std::exchange(stopToken_, &token_)) {}
        ~CancellationScope() { stopToken_ = previous_; }
        CancellationScope(const CancellationScope&) = delete;
        CancellationScope& operator=(const CancellationScope&) = delete;
    };

    // Post func to the game thread, block until it returns the result. Releases GameReadLock while waiting.
    template <typename F>
    static auto Execute(F func, std::source_location loc = std::source_location::current()) -> decltype(func()) {
        using T = decltype(func());
        const auto stopToken = stopToken_ ? *stopToken_ : std::stop_token{};

        if (stopToken.stop_requested()) {
            throw Cancelled{};
        }

        if (GameWriteLock::IsHeldByCurrentThread()) {
            if constexpr (std::is_void_v<T>) {
                func();
                return;
            } else {
                return func();
            }
        }

        auto state = std::make_shared<CallState<T>>();
        const std::stop_callback cancellation(stopToken, [state] { state->Cancel(); });
        {
            const std::lock_guard lock(queueMutex_);
            queue_.push({[state, &func] {
                             if (!state->Start()) {
                                 return;
                             }
                             // Once dispatch starts, the caller must wait until all
                             // access to its referenced data ends, even if stopped meanwhile.
                             try {
                                 if constexpr (std::is_void_v<T>) {
                                     func();
                                 } else {
                                     state->result.emplace(func());
                                 }
                             } catch (...) {
                                 state->error = std::current_exception();
                             }
                             state->Finish();
                         },
                         loc});
        }

        // A wait, not work: descheduled until the game thread's next drain, which can be most of a
        // frame, and not the posting binding's CPU cost. Declared first so it also covers the
        // releaser's re-acquire.
        const utils::profiling::ScopedSleep waiting;
        // Release game read lock so the game thread's frame-advance write lock
        // can proceed. Re-acquires on scope exit.
        GameReadLockReleaser releaser;
        host::WaitForTask(state->state);
        if (state->state.load(std::memory_order_acquire) == host::TaskState::Cancelled) {
            throw Cancelled{};
        }
        if (state->error) {
            std::rethrow_exception(state->error);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*state->result);
        }
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

    // Whether a task waits for the next Drain. Only used by the simulator; d2bs.dll never calls it.
    static bool HasPending() {
        const std::lock_guard lock(queueMutex_);
        return !queue_.empty();
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
            utils::threads::CrashContextScope scope(std::format(
                "GameThread task from {}:{} ({})", std::filesystem::path(task.loc.file_name()).filename().string(),
                task.loc.line(), task.loc.function_name()));
            task.func();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (elapsed >= SLOW_TASK_THRESHOLD) {
                Log().warn("[GameThread] slow task: {}ms from {}:{} ({})", elapsed.count(),
                           std::filesystem::path(task.loc.file_name()).filename().string(), task.loc.line(),
                           task.loc.function_name());
            }
            tasks.pop();
        }
    }
};

}  // namespace d2bs::game
