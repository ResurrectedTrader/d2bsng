#pragma once

#include <spdlog/logger.h>
#include <v8.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include "ScriptTypes.h"
#include "game/Types.h"

namespace d2bs::framework::drawing {
struct Drawable;
}  // namespace d2bs::framework::drawing

namespace d2bs::framework::inspector {
class ScriptInspector;
}  // namespace d2bs::framework::inspector

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

    // V8 access - returns a refcounted copy so the isolate stays alive for
    // the duration of the caller's use, even if TeardownIsolate runs concurrently.
    [[nodiscard]] std::shared_ptr<v8::Isolate> GetIsolate() const { return isolate_.load(); }

    // The script's V8 context. Only valid on the script's own thread (a v8::Local
    // requires a HandleScope on the isolate's thread). Empty before SetupIsolate
    // and after TeardownIsolate.
    [[nodiscard]] v8::Local<v8::Context> GetContext() const;

    // Canonical cancellation signal - fires when Stop() is called.
    [[nodiscard]] std::stop_token GetStopToken() const { return thread_.get_stop_token(); }

    // Heap stats cached on the script's own thread (safe to read cross-thread).
    // Updated periodically (~1s), not on every event loop tick.
    [[nodiscard]] std::shared_ptr<v8::HeapStatistics> GetCachedHeapStats() const { return cachedHeapStats_.load(); }
    // Force a fresh snapshot - only safe from the script's own thread. `now` is
    // the caller's single steady_clock reading for the pass (steady_clock::now()
    // is QueryPerformanceCounter on MSVC, so event-loop callers pass theirs in
    // rather than taking a second hooked reading).
    void UpdateHeapStats(std::chrono::steady_clock::time_point now, bool force = false);

    // Last-known JS call stack, refreshed per StackCaptureMode (Off by default,
    // so nothing is captured unless the Stacktraces panel selected this script).
    // Callable from any thread.
    [[nodiscard]] std::shared_ptr<StackTraceSnapshot> GetLastStackTrace() const { return lastStackTrace_.load(); }
    // Walk this script's V8 stack and replace the cache. Owner-thread only -
    // invoked from delay() and the JS->native trampolines per StackCaptureMode.
    void RefreshLastStackTrace(int32_t maxFrames = 64);

    // Controls how often RefreshLastStackTrace runs (see StackCaptureMode). Off
    // by default; the console's Stacktraces panel raises the selected script to
    // OnYield / OnEveryCall and drops it to Off on deselect or console hide.
    // Cross-thread safe.
    void SetStackCaptureMode(StackCaptureMode mode);
    [[nodiscard]] StackCaptureMode GetStackCaptureMode() const {
        return stackCaptureMode_.load(std::memory_order_acquire);
    }

    // Ask V8 to garbage-collect this script's isolate. Safe to call from
    // any thread. From the script's own thread (e.g. TeardownIsolate) the
    // GC runs synchronously; from any other thread (e.g. the console
    // GC button) it's scheduled via RequestInterrupt and runs the next
    // time V8 hits a safe point. No-op if the script has no isolate
    // (Stopped state).
    void RequestGarbageCollection() const;

    // Evaluate `code` as JS on this script's isolate. Enqueues an
    // EvaluateEvent on the script's event queue - the evaluation runs on
    // the script's thread, not the caller's. Thread-safe.
    void Evaluate(const std::string& code);

    // Event system
    void RegisterEvent(const std::string& eventName, v8::Local<v8::Function> func);
    void UnregisterEvent(const std::string& eventName, v8::Local<v8::Function> func);
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
    // this script. Added and removed from V8 callbacks on the script's own
    // thread; iterated by the game thread via GetDrawables() for draw / hit
    // testing across all scripts.
    //
    // ~Drawable calls Reset() on its v8::Global onClick/onHover handles; that
    // reaches into the isolate's GlobalHandles and UAFs after Isolate::Dispose.
    // To make ~Drawable safe on any thread at any time, RemoveDrawable and
    // TeardownIsolate explicitly Reset() the Globals on the script thread
    // (under drawablesMutex_ while the isolate is still alive) before dropping
    // the script's own shared_ptr. If a game-thread reader still holds a copy
    // via GetDrawables(), its later ~Drawable finds empty Globals and the
    // Reset becomes a no-op.
    void AddDrawable(std::shared_ptr<framework::drawing::Drawable> drawable);
    // Remove a drawable, pre-Reset its v8::Globals, and run its onDestroy
    // hook on the script thread. If `fireLeaveEvent` is true and the drawable
    // was hovered, dispatches a hover-leave ScreenHookHoverEvent after
    // releasing the lock. TeardownIsolate passes false - the script's event
    // loop has exited and a same-thread ExecuteEvent would synchronously run
    // user JS that's about to be torn down.
    void RemoveDrawable(const std::shared_ptr<framework::drawing::Drawable>& drawable, bool fireLeaveEvent = true);
    [[nodiscard]] std::vector<std::shared_ptr<framework::drawing::Drawable>> GetDrawables();

   private:
    void ThreadMain(const std::stop_token& stopToken);
    void SetupIsolate();
    void TeardownIsolate();
    void RunScript();
    void ReportException(v8::TryCatch& tryCatch);

    // Create and register this isolate's ScriptInspector (Chrome DevTools
    // target). Called once from SetupIsolate, on the isolate's own thread.
    void AttachInspector();

    // Detach thread and remove self from ScriptEngine map (safe for self-destruction)
    void RemoveSelfFromEngine();

    std::filesystem::path path_;
    std::filesystem::path normalizedPath_;  // Lowercase for case-insensitive comparison
    ScriptMode mode_;
    std::vector<std::vector<uint8_t>> args_;
    std::atomic<ScriptState> state_{ScriptState::Stopped};
    std::shared_ptr<spdlog::logger> logger_;

    std::jthread thread_;
    // Shared ownership prevents use-after-dispose: Stop() and PostEvent() (called
    // from external threads) atomically load a refcounted copy, keeping the isolate
    // alive for the duration of their call.  TeardownIsolate() atomically exchanges
    // to nullptr and performs cleanup; the custom deleter (NotifyIsolateShutdown +
    // Dispose) fires when the last copy drops.
    //
    // The shared_ptr must be wrapped in std::atomic (C++20) because the shared_ptr
    // object itself is not thread-safe: its internal refcount is atomic, but
    // concurrent read (load/copy) and write (store/exchange) of the same shared_ptr
    // instance is UB without external synchronization.  std::atomic<shared_ptr>
    // provides that synchronization without an explicit mutex.
    std::atomic<std::shared_ptr<v8::Isolate>> isolate_;
    v8::Global<v8::Context> context_;

    std::atomic<std::shared_ptr<v8::HeapStatistics>> cachedHeapStats_;
    std::chrono::steady_clock::time_point lastHeapStatsUpdate_;
    std::atomic<std::shared_ptr<StackTraceSnapshot>> lastStackTrace_;
    std::atomic<StackCaptureMode> stackCaptureMode_{StackCaptureMode::Off};
    std::atomic<uint32_t> nativeThreadId_{0};

    // Event registry
    std::mutex eventFunctionsMutex_;
    std::unordered_map<std::string, std::vector<v8::Global<v8::Function>>> eventFunctions_;

    // Delayed events (setTimeout/setInterval)
    std::mutex delayedEventMutex_;
    std::unordered_map<uint32_t, std::shared_ptr<DelayedEvent>> delayedEvents_;

    // Include tracking (no lock needed - includes only happen on script's own thread)
    std::set<std::filesystem::path> includes_;
    std::set<std::filesystem::path> inProgressIncludes_;

    std::shared_mutex drawablesMutex_;
    std::vector<std::shared_ptr<framework::drawing::Drawable>> drawables_;

    // V8 inspector (Chrome DevTools) attachment for this isolate. Created by
    // AttachInspector in SetupIsolate and destroyed in TeardownIsolate, both on
    // the script's own thread; touched only there.
    std::unique_ptr<framework::inspector::ScriptInspector> inspector_;
};

}  // namespace d2bs
