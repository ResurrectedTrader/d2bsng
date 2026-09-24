#pragma once

#include <spdlog/logger.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ScriptTypes.h"
#include "game/Types.h"
#include "unibind/unibind.h"
#include "utils/Profiling.h"

namespace d2bs::runtime::drawing {
struct Drawable;
}  // namespace d2bs::runtime::drawing

namespace d2bs::runtime::inspector {
class ScriptInspector;
}  // namespace d2bs::runtime::inspector

namespace d2bs {

class BaseEvent;
class DelayedEvent;

// Single JS stack frame. functionName / scriptName may be empty; the
// renderer substitutes placeholders ("<anonymous>" / "<unknown>") at draw
// time so the data model stays raw.
struct StackFrame {
    std::string functionName;
    std::string scriptName;
    int32_t line = 0;
    int32_t column = 0;
};

// Captured JS call stack of a script's isolate. Produced by
// Script::RefreshLastStackTrace (see StackCaptureMode for when);
// read cross-thread via Script::GetLastStackTrace.
struct StackTraceSnapshot {
    std::vector<StackFrame> frames;
};

// How often a script refreshes its cached JS stack (StackTraceSnapshot). Off by
// default - capture is opt-in via the console's Stacktraces panel, which raises
// the selected script to OnYield or OnEveryCall and drops it back to Off on
// deselect or when the console is hidden.
enum class StackCaptureMode : uint8_t {
    Off,          // never walk the stack (zero cost)
    OnYield,      // refresh at delay() yields only
    OnEveryCall,  // also refresh on every JS->native callback
};

class Script : public std::enable_shared_from_this<Script> {
   public:
    Script(std::filesystem::path path, ScriptMode mode, std::vector<std::vector<uint8_t>> args = {});
    ~Script();

    // Non-copyable, non-movable
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;
    Script(Script&&) = delete;
    Script& operator=(Script&&) = delete;

    // Lifecycle
    void Start();
    void Stop();
    void Pause();
    void Resume();
    void Join();

    // State (thread-safe)
    [[nodiscard]] ScriptState GetState() const { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] ScriptMode GetMode() const { return mode_; }

    // Whether this script's drawables should render / receive input when the
    // client is in `state`.  InGame-mode scripts match GameState::InGame,
    // OutOfGame-mode match GameState::Menu, Console-mode matches always.
    // Transitional client states (Busy / Null) match nothing.
    [[nodiscard]] bool DrawablesVisibleIn(game::GameState state) const {
        switch (mode_) {
            case ScriptMode::Console:
                return true;
            case ScriptMode::OutOfGame:
                return state == game::GameState::Menu;
            case ScriptMode::InGame:
                return state == game::GameState::InGame;
        }
        return false;
    }

    // Identity
    [[nodiscard]] const std::filesystem::path& GetPath() const { return path_; }
    [[nodiscard]] const std::filesystem::path& GetNormalizedPath() const { return normalizedPath_; }
    [[nodiscard]] std::string GetName() const;
    [[nodiscard]] std::thread::id GetThreadId() const;
    [[nodiscard]] uint32_t GetNativeThreadId() const { return nativeThreadId_.load(std::memory_order_relaxed); }

    // Engine access - returns a refcounted copy so the isolate stays alive for
    // the duration of the caller's use, even if TeardownIsolate runs concurrently.
    // Only the calls unibind allows from any thread (TerminateExecution,
    // RequestInterrupt, PostJob) may be made off the script's thread, and a
    // copy must be dropped promptly: TeardownIsolate waits for the last one so
    // the isolate is destroyed on its own thread.
    [[nodiscard]] std::shared_ptr<ub::Isolate> GetIsolate() const { return isolate_.load(); }

    // Whether this script's execution environment still exists, so posted
    // events can still run. False before SetupIsolate and after TeardownIsolate.
    [[nodiscard]] bool IsAlive() const { return isolate_.load() != nullptr; }

    // The script's realm. Script thread only, like every value read out of it.
    // Empty before SetupIsolate and after TeardownIsolate.
    [[nodiscard]] const ub::Context& GetContext() const { return context_; }

    // Canonical cancellation signal - fires when Stop() is called.
    [[nodiscard]] std::stop_token GetStopToken() const { return thread_.get_stop_token(); }

    // Heap stats cached on the script's own thread (safe to read cross-thread).
    // Updated periodically (~1s), not on every event loop tick.
    [[nodiscard]] std::shared_ptr<ub::HeapStatistics> GetCachedHeapStats() const { return cachedHeapStats_.load(); }
    // Force a fresh snapshot - only safe from the script's own thread. `now` is
    // the caller's single steady_clock reading for the pass (steady_clock::now()
    // is QueryPerformanceCounter on MSVC, so event-loop callers pass theirs in
    // rather than taking a second hooked reading).
    void UpdateHeapStats(std::chrono::steady_clock::time_point now, bool force = false);

    // Last-known JS call stack, refreshed per StackCaptureMode (Off by default,
    // so nothing is captured unless the Stacktraces panel selected this script).
    // Callable from any thread.
    [[nodiscard]] std::shared_ptr<StackTraceSnapshot> GetLastStackTrace() const { return lastStackTrace_.load(); }
    // Walk this script's JS stack and replace the cache. Owner-thread only -
    // invoked from delay() and the JS->native trampolines per StackCaptureMode,
    // and from an interrupt when capture is switched on from another thread.
    void RefreshLastStackTrace(int32_t maxFrames = 64);

    // Controls how often RefreshLastStackTrace runs (see StackCaptureMode). Off
    // by default; the console's Stacktraces panel raises the selected script to
    // OnYield / OnEveryCall and drops it to Off on deselect or console hide.
    // Cross-thread safe.
    void SetStackCaptureMode(StackCaptureMode mode);
    [[nodiscard]] StackCaptureMode GetStackCaptureMode() const {
        return stackCaptureMode_.load(std::memory_order_acquire);
    }

    // Ask the engine to garbage-collect this script's isolate. Safe to call
    // from any thread. From the script's own thread the request is made
    // directly; from any other thread (e.g. the console GC button) it's
    // scheduled via RequestInterrupt and made the next time the engine
    // checks for interrupts inside running script. No-op if the script has
    // no isolate (Stopped state).
    void RequestGarbageCollection() const;

    // Evaluate `code` as JS on this script's isolate. Enqueues an
    // EvaluateEvent on the script's event queue - the evaluation runs on
    // the script's thread, not the caller's. Thread-safe.
    void Evaluate(const std::string& code);

    // Event system
    void RegisterEvent(const std::string& eventName, const ub::Local<ub::Function>& func);
    void UnregisterEvent(const std::string& eventName, const ub::Local<ub::Function>& func);
    [[nodiscard]] bool IsEventRegistered(std::string_view eventName);
    void ClearEvent(const std::string& eventName);
    void ClearAllEvents();

    bool ExecuteEvent(const std::shared_ptr<BaseEvent>& event);
    bool PostEvent(const std::shared_ptr<BaseEvent>& event, uint32_t delayMs = 0);
    void ExecuteEvents(std::chrono::milliseconds duration);

    // Delayed events (timers)
    void AddDelayedEvent(const std::shared_ptr<DelayedEvent>& event);
    bool RemoveDelayedEvent(uint32_t eventId);

    // Logger - per-script logger named after the script filename.
    // Use GetScriptLogger(isolate) from callbacks for convenient access.
    [[nodiscard]] const std::shared_ptr<spdlog::logger>& GetLogger() const { return logger_; }

    // Include system
    bool Include(const std::filesystem::path& absolutePath);
    [[nodiscard]] bool IsIncluded(const std::filesystem::path& absolutePath) const;

    // Normalize path for case-insensitive comparison on Windows
    [[nodiscard]] static std::filesystem::path NormalizePath(const std::filesystem::path& path);

    // Drawables (screen hooks: boxes, frames, lines, text, images) owned by
    // this script. Added and removed from native callbacks on the script's own
    // thread; iterated by the game thread via GetDrawables() for draw / hit
    // testing across all scripts.
    void AddDrawable(std::shared_ptr<runtime::drawing::Drawable> drawable);
    // Remove a drawable, drop its handlers, and run its onDestroy hook on the
    // script thread. If `fireLeaveEvent` is true and the drawable was hovered,
    // dispatches a hover-leave ScreenHookHoverEvent after releasing the lock.
    // TeardownIsolate passes false - the script's event loop has exited and a
    // same-thread ExecuteEvent would synchronously run user JS that's about to
    // be torn down.
    void RemoveDrawable(const std::shared_ptr<runtime::drawing::Drawable>& drawable, bool fireLeaveEvent = true);
    [[nodiscard]] std::vector<std::shared_ptr<runtime::drawing::Drawable>> GetDrawables();

    // A drawable's click / hover callbacks live here rather than on the
    // drawable, so the root is only ever created and destroyed on this
    // script's thread - the game thread holds shared_ptr<Drawable> copies
    // across a frame, which would otherwise release a GC root off-thread.
    // The drawable carries the matching hasClick / hasHover flag for
    // game-thread hit testing. An empty `handler` clears the slot.
    void SetDrawableHandler(runtime::drawing::Drawable& drawable, DrawableHandler which,
                            const ub::Local<ub::Function>& handler);
    [[nodiscard]] std::optional<ub::Local<ub::Function>> GetDrawableHandler(const runtime::drawing::Drawable& drawable,
                                                                            DrawableHandler which);

    // Game thread. Runs the drawable's handler on this script's event loop.
    // The event names the drawable and keeps it alive; the handler itself is
    // resolved once the event reaches this script's thread, so nothing here
    // touches a script value. The click variant waits for the handler's block
    // vote and returns it; false if the handler is gone or the script is
    // tearing down.
    bool DispatchDrawableClick(std::shared_ptr<const runtime::drawing::Drawable> drawable, game::ClickButton button,
                               game::Point pos);
    void DispatchDrawableHover(std::shared_ptr<const runtime::drawing::Drawable> drawable, game::Point pos,
                               bool entered);

   private:
    // Drops every handler and its listener counts. Caller holds eventFunctionsMutex_.
    void ClearEventFunctionsLocked();

    void ThreadMain(const std::stop_token& stopToken);
    void SetupIsolate();
    void TeardownIsolate();
    void RunScript();
    void ReportException(const ub::TryCatch& tryCatch);

    // Attach this script to the engine's debugger, if it has one. Called once
    // from SetupIsolate, on the isolate's own thread, with the context entered.
    void AttachDebugger(ub::Isolate& isolate);

    // An event posted to this script's isolate and not yet run. Owned here
    // rather than by the job queue, which drops what it has not run when the
    // isolate goes: TeardownIsolate frees whatever is left, and an event that
    // never ran is told so (OnDropped) on the way out.
    struct PendingJob {
        explicit PendingJob(std::shared_ptr<BaseEvent> event) : event(std::move(event)) {}
        ~PendingJob();
        PendingJob(const PendingJob&) = delete;
        PendingJob& operator=(const PendingJob&) = delete;
        PendingJob(PendingJob&&) = delete;
        PendingJob& operator=(PendingJob&&) = delete;

        std::shared_ptr<BaseEvent> event;
        bool isDelivered = false;
    };
    static void RunPendingJob(ub::Isolate& isolate, ub::CallbackData data);

    // Detach thread and remove self from ScriptEngine map (safe for self-destruction)
    void RemoveSelfFromEngine();

    std::filesystem::path path_;
    std::filesystem::path normalizedPath_;  // Lowercase for case-insensitive comparison
    // Where this thread's time goes inside delay(), for the Profiling panel. Script-thread-only.
    profiling::Timeline idle_;
    ScriptMode mode_;
    std::vector<std::vector<uint8_t>> args_;
    std::atomic<ScriptState> state_{ScriptState::Stopped};
    std::shared_ptr<spdlog::logger> logger_;

    std::jthread thread_;
    // Shared ownership prevents use-after-destroy: Stop() and PostEvent() (called
    // from external threads) atomically load a refcounted copy, keeping the isolate
    // alive for the duration of their call.  TeardownIsolate() atomically exchanges
    // to nullptr, waits for those copies to drop, and destroys the isolate on the
    // script's own thread, which is the only thread that may.
    //
    // The shared_ptr must be wrapped in std::atomic (C++20) because the shared_ptr
    // object itself is not thread-safe: its internal refcount is atomic, but
    // concurrent read (load/copy) and write (store/exchange) of the same shared_ptr
    // instance is UB without external synchronization.  std::atomic<shared_ptr>
    // provides that synchronization without an explicit mutex.
    std::atomic<std::shared_ptr<ub::Isolate>> isolate_;
    ub::Context context_;

    std::atomic<std::shared_ptr<ub::HeapStatistics>> cachedHeapStats_;
    std::chrono::steady_clock::time_point lastHeapStatsUpdate_;
    std::atomic<std::shared_ptr<StackTraceSnapshot>> lastStackTrace_;
    std::atomic<StackCaptureMode> stackCaptureMode_{StackCaptureMode::Off};
    std::atomic<uint32_t> nativeThreadId_{0};

    // Event registry
    std::mutex eventFunctionsMutex_;
    std::unordered_map<std::string, std::vector<ub::Global<ub::Function>>> eventFunctions_;

    // Delayed events (setTimeout/setInterval)
    std::mutex delayedEventMutex_;
    std::unordered_map<uint32_t, std::shared_ptr<DelayedEvent>> delayedEvents_;

    // Include tracking (no lock needed - includes only happen on script's own thread)
    std::set<std::filesystem::path> includes_;
    std::set<std::filesystem::path> inProgressIncludes_;

    struct DrawableHandlers {
        ub::Global<ub::Function> click;
        ub::Global<ub::Function> hover;
    };

    std::shared_mutex drawablesMutex_;
    std::vector<std::shared_ptr<runtime::drawing::Drawable>> drawables_;
    // Keyed by drawable identity; the entry is erased in RemoveDrawable before
    // the script's own shared_ptr drops, so a key never outlives its drawable.
    std::unordered_map<const runtime::drawing::Drawable*, DrawableHandlers> drawableHandlers_;

    // Posted events not yet run, keyed by the pointer the job carries. Filled
    // by PostEvent on any thread, emptied by RunPendingJob and TeardownIsolate
    // on the script's own.
    std::mutex pendingJobsMutex_;
    std::unordered_map<PendingJob*, std::unique_ptr<PendingJob>> pendingJobs_;

    // Debugger attachment for this isolate; null when the engine has none.
    // Created by AttachDebugger in SetupIsolate and destroyed in
    // TeardownIsolate, both on the script's own thread; touched only there.
    std::unique_ptr<runtime::inspector::ScriptInspector> inspector_;
};

}  // namespace d2bs
