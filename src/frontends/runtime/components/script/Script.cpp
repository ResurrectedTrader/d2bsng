#include "Script.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>

#include "api/classes/ClassRegistry.h"
#include "api/core/InstanceTracker.h"
#include "api/globals/Constants.h"
#include "api/globals/CoreFunctions.h"
#include "api/globals/GameFunctions.h"
#include "api/globals/HashFunctions.h"
#include "api/globals/MenuFunctions.h"
#include "components/drawing/Drawable.h"
#include "components/engine/Engine.h"
#include "components/events/BaseEvent.h"
#include "components/events/DelayedEvent.h"
#include "components/events/EventDispatch.h"
#include "components/events/Events.h"
#include "components/inspector/ScriptInspector.h"
#include "components/script/CompileSource.h"
#include "components/script/NativeCallHook.h"
#include "components/script/ScriptEngine.h"
#include "components/script/ScriptLogger.h"
#include "config/AppConfig.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "speedhack/Speedhack.h"
#include "utils/DeferGuard.h"
#include "utils/Profiling.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs {

namespace {

// Where a script thread's time goes while it is inside delay(). Indexed by IdlePhase. The JS run
// between delay() calls is outside this loop and does not appear; the thread row's CPU covers it.
enum class IdlePhase : size_t { Pump, Handlers, Wait, Paused };

constexpr std::array IDLE_PHASES = {
    profiling::PhaseInfo{.name = "event pump",
                         .what = "engine jobs, debugger messages, heap stats - everything but the handlers",
                         .warn = 5.0,
                         .bad = 15.0},
    profiling::PhaseInfo{.name = "handlers (JS)", .what = "event and timer callbacks run from delay()"},
    profiling::PhaseInfo{.name = "asleep", .what = "the wait the script asked for", .blocking = true},
    profiling::PhaseInfo{.name = "paused", .what = "paused from the console", .blocking = true},
};

// One timeline per script thread; the panel sums the ones sharing this title into one table.
constexpr profiling::TimelineInfo IDLE_TIMELINE{
    .title = "Script threads, inside delay()",
    .phases = IDLE_PHASES,
    .framePhase = static_cast<size_t>(IdlePhase::Pump),
    .frameLabel = "passes",
    .idle = "No script called delay() in this window.",
    .order = 300,
};

}  // namespace

Script::Script(std::filesystem::path path, ScriptMode mode, std::vector<std::vector<uint8_t>> args)
    : path_(std::move(path)),
      normalizedPath_(NormalizePath(path_)),
      idle_(IDLE_TIMELINE),
      mode_(mode),
      args_(std::move(args)),
      logger_(utils::GetLogger(path_.filename().string())) {}

std::shared_ptr<spdlog::logger> GetLogger(ub::Isolate* isolate) {
    if (auto* script = ScriptEngine::Instance().GetScript(isolate)) {
        return script->GetLogger();
    }
    return utils::GetLogger("js");
}

// NOLINTNEXTLINE(bugprone-exception-escape) - join() can throw std::system_error; terminate is fine at teardown
Script::~Script() {
    Stop();
    // Release any per-call stack-capture slot this script held so the global
    // count can't leak if it dies while still selected in the console.
    SetStackCaptureMode(StackCaptureMode::Off);
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id()) {
        // Being destroyed from our own thread (natural completion via RemoveSelfFromEngine
        // dropping the last shared_ptr). Detach to prevent ~jthread from self-joining.
        thread_.detach();
    } else {
        Join();
    }
}

void Script::Start() {
    auto expected = ScriptState::Stopped;
    if (!state_.compare_exchange_strong(expected, ScriptState::Starting)) {
        return;  // Not in Stopped state - already running or stopping
    }
    thread_ = std::jthread([this](const std::stop_token& token) { ThreadMain(token); });
}

void Script::Stop() {
    // Atomically transition to Stopping from any active state
    auto current = GetState();
    while (current != ScriptState::Stopped && current != ScriptState::Stopping) {
        if (state_.compare_exchange_weak(current, ScriptState::Stopping)) {
            thread_.request_stop();

            // Safe to call TerminateExecution if isolate was initialized
            // (Ready or later state). The shared_ptr copy keeps the
            // isolate alive for the duration of this call, even if
            // TeardownIsolate runs concurrently on the script thread.
            if (current >= ScriptState::Ready) {
                auto iso = isolate_.load();
                if (iso) {
                    iso->TerminateExecution();
                }
            }
            return;
        }
        // CAS failed, current was reloaded - retry
    }
}

void Script::Pause() {
    auto expected = ScriptState::Running;
    state_.compare_exchange_strong(expected, ScriptState::Paused);
}

void Script::Resume() {
    auto expected = ScriptState::Paused;
    state_.compare_exchange_strong(expected, ScriptState::Running);
}

void Script::Join() {
    if (thread_.joinable()) {
        // Release game locks while blocked: the target thread may need to
        // acquire GameReadLock (script-side) or GameWriteLock-protected drain
        // work (game-side) to make progress and exit. Without the releasers,
        // the join can close an AB-BA cycle - e.g. game thread joining a script
        // that's blocked on GameReadLock, or a script holding GameReadLock
        // joining a thread that needs GameThread::Execute to drain.
        // Each releaser is a no-op when its lock isn't held on this thread.
        game::GameWriteLockReleaser writeReleaser;
        game::GameReadLockReleaser readReleaser;
        thread_.join();
    }
}

void Script::RemoveSelfFromEngine() {
    // Console scripts are managed by RestartConsoleScript/Shutdown.
    if (mode_ != ScriptMode::Console) {
        ScriptEngine::Instance().RemoveScript(std::this_thread::get_id());
    }
}

std::string Script::GetName() const {
    return config::GetAppConfig().GetScriptPaths().RelativeScriptPath(path_);
}

std::thread::id Script::GetThreadId() const {
    return thread_.get_id();
}

void Script::Evaluate(const std::string& code) {
    auto event = std::make_shared<EvaluateEvent>(code);
    ExecuteEvent(event);
}

void Script::AttachDebugger(ub::Isolate& isolate) {
    // Label shown in the debugger's target list. Prefix with the active profile
    // (when set) so multi-box users can tell which bot a target belongs to. The
    // console script carries a real path (its lookup fallback), so classify by mode.
    const bool isConsole = mode_ == ScriptMode::Console;
    std::string name = isConsole ? std::string("Console") : GetName();
    const std::string profile = config::GetAppConfig().GetProfileName();
    std::string title = profile.empty() ? name : (profile + " / " + name);
    // The url is the base-relative path as a file:// URL, like every script URL
    // the debugger sees (it maps script origins the same way): the install path
    // stays out of the debugger and the URLs are stable across machines.
    std::string url =
        isConsole ? std::string("d2bs://console") : config::GetAppConfig().GetScriptPaths().FileUrl(path_);
    inspector_ = runtime::inspector::ScriptInspector::Create(*this, context_, std::move(title), std::move(url));
}

void Script::ThreadMain(const std::stop_token& stopToken) {
    // Keep ourselves alive for the entire duration of ThreadMain.
    // RemoveSelfFromEngine() drops the engine map's shared_ptr, which may be the
    // last external reference.  Without this, `this` is destroyed mid-function.
    auto self = shared_from_this();

    ScriptEngine::currentScript_ = this;
    const DeferGuard clearCurrent([] { ScriptEngine::currentScript_ = nullptr; });

    // Store native Win32 thread ID for JS threadid property (avoids hash truncation)
    nativeThreadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);

    thread_utils::SetThreadDescription(GetName());

    // Script threads run user JS; their Date.now / delay / setTimeout
    // should observe the global time multiplier. The engine's internal worker
    // pool runs on threads we never touch, so those stay on real time.
    speedhack::OptInCurrentThread();

    logger_->debug("Thread starting: {}", path_.string());

    if (stopToken.stop_requested()) {
        logger_->info("Stopped before initialization");
        RemoveSelfFromEngine();
        state_.store(ScriptState::Stopped, std::memory_order_release);
        return;
    }

    SetupIsolate();
    // Signal that isolate is ready - Stop() can now safely call TerminateExecution.
    // CAS avoids overwriting Stopping if Stop() raced us.
    auto expected = ScriptState::Starting;
    state_.compare_exchange_strong(expected, ScriptState::Ready);

    expected = ScriptState::Ready;
    if (state_.compare_exchange_strong(expected, ScriptState::Running)) {
        RunScript();
    }

    // Transition to Stopping if not already (Stop() may have beaten us).
    expected = ScriptState::Running;
    state_.compare_exchange_strong(expected, ScriptState::Stopping);

    // Remove from the engine map BEFORE tearing down the isolate.
    // This ensures no cross-thread caller (e.g. PostEvent) can obtain
    // our isolate pointer from the map after we start disposing it.
    RemoveSelfFromEngine();

    TeardownIsolate();
    state_.store(ScriptState::Stopped, std::memory_order_release);

    logger_->debug("Thread exiting");
}

namespace {

// Install a `console` accessor that routes by caller (see the call site in
// SetupIsolate for the rationale). Returns false on failure. Must run with
// `context` entered.
bool InstallConsoleRouting(const ub::Context& context) {
    // Native flag-reader the shim closes over: true while the debugger is
    // evaluating an expression its client typed.
    auto evaluating = ub::Function::New(
        context, +[](const ub::CallbackInfo& info) {
            info.GetReturnValue().Set(runtime::inspector::ScriptInspector::IsEvaluating());
        });
    if (!evaluating || !context.GlobalObject().Set(context, "__d2bsDebuggerEvaluating", *evaluating).value_or(false)) {
        return false;
    }
    // Stash the engine's built-in console, if it has one, and redefine `console`
    // as an accessor: a debugger evaluate gets the engine's console (so
    // console.log lands in the debugger client's console), while normal script
    // execution gets whatever console the script installs - captured by the
    // setter. kolbot's `global.console = global.console || polyfill()` reads
    // undefined at script time and installs its print-routed polyfill, which
    // the setter captures. On an engine with no built-in console there is
    // nothing to route to, and `console` is simply the script's. The native
    // helper is deleted once the shim closes over it.
    static constexpr std::string_view CONSOLE_SHIM = R"JS(
(function () {
  const engineConsole = globalThis.console;
  let scriptConsole;
  const debuggerEvaluating = globalThis.__d2bsDebuggerEvaluating;
  delete globalThis.__d2bsDebuggerEvaluating;
  Object.defineProperty(globalThis, 'console', {
    configurable: true,
    get() { return engineConsole !== undefined && debuggerEvaluating() ? engineConsole : scriptConsole; },
    set(v) { scriptConsole = v; },
  });
})();
)JS";
    return ub::Evaluate(context, CONSOLE_SHIM).has_value();
}

}  // namespace

void Script::SetupIsolate() {
    (void)Engine::GetPlatform();

    auto created = ub::Isolate::New(ub::IsolateOptions{.heapLimitBytes = config::GetAppConfig().memoryLimit});
    if (!created) {
        logger_->error("Failed to create the script's isolate");
        return;
    }
    // Shared so cross-thread callers (Stop, PostEvent) can hold it for the
    // length of one call; TeardownIsolate waits those out and destroys it here.
    std::shared_ptr<ub::Isolate> iso(std::move(created));
    // Resolves ScriptEngine::GetScript(isolate) for every callback.
    iso->SetEmbedderData(*this);
    isolate_.store(iso);

    const ub::HandleScope handleScope(*iso);
    auto context = ub::Context::New(*iso);
    if (!context) {
        logger_->error("Failed to create the script's context");
        return;
    }
    context_ = std::move(*context);
    const ub::ContextScope contextScope(context_);
    const ub::TryCatch tryCatch(*iso);

    // Class constructors (Unit, Room, File, etc.), global functions and
    // constants (FILE_READ, ...), installed on the global object.
    if (!api::classes::RegisterAllClasses(context_)) {
        logger_->error("Failed to register classes: {}", tryCatch.Message(context_).value_or("<no message>"));
        return;
    }
    api::globals::RegisterCoreFunctions(context_);
    api::globals::RegisterGameFunctions(context_);
    api::globals::RegisterMenuFunctions(context_);
    api::globals::RegisterHashFunctions(context_);
    api::globals::RegisterConstants(context_);

    // Always attach this script to the debugger, if the engine has one. Done
    // before the console shim below so that shim captures the engine's
    // debugger-wired console for the debugger's evaluate path. Attaching costs
    // nothing measurable until a client connects, and the server only exposes
    // the target when running (toggled by inspectorPort's sign via the Settings
    // panel / SetInspector).
    AttachDebugger(*iso);

    // Route `console` by caller. V8 installs a built-in `console` on every
    // context, wired to its debugger; SpiderMonkey has none. Kolbot's
    // Polyfill.js installs its own print-routed console via
    // `global.console = global.console || (...)()`, which only fires if
    // `console` reads falsy at script time. We want both: a script's
    // console.log -> the script's polyfill (d2bs console), and console.log
    // typed in the debugger client -> the engine's console. Since both share one
    // global, InstallConsoleRouting makes `console` an accessor that returns the
    // engine's console while the debugger is evaluating (ScriptInspector::IsEvaluating)
    // and the script's console otherwise. See docs/inspector.md.
    if (!InstallConsoleRouting(context_)) {
        logger_->error("Failed to install console routing: {}", tryCatch.Message(context_).value_or("<no message>"));
        return;
    }

    // The 'me' global object (player unit with extra properties).
    auto me = api::classes::CreateMeObject(context_);
    if (!me || !context_.GlobalObject().Set(context_, "me", *me).value_or(false)) {
        logger_->error("Failed to create 'me' global object");
        return;
    }
    runtime::script::ApplyCompatibilityPrelude(context_);
}

void Script::TeardownIsolate() {
    // RemoveDrawable handles per-drawable cleanup (drop the handlers, run
    // onDestroy on this thread). fireLeaveEvent=false skips the synchronous
    // hover-leave dispatch - the script's event loop has exited and a same-
    // thread ExecuteEvent would run user JS that's about to be torn down.
    for (const auto& drawable : GetDrawables()) {
        RemoveDrawable(drawable, /*fireLeaveEvent=*/false);
    }
    // isolate_ has its own atomic sync - no need to cover the exchange with
    // drawablesMutex_. Cross-thread callers (Stop/PostEvent) racing the
    // exchange see nullptr post-swap and bail out.
    auto iso = isolate_.exchange(std::shared_ptr<ub::Isolate>());
    if (!iso) {
        return;
    }
    // Callers that loaded the isolate before the exchange hold a copy for the
    // length of one call. Wait them out: an isolate may only be destroyed on
    // its own thread, and once they are gone nothing can post to it any more.
    // Nothing may hold a copy for longer - a copy that never goes stalls the
    // script's thread here, so a slow wait is logged.
    const auto waitStart = std::chrono::steady_clock::now();
    bool warned = false;
    while (iso.use_count() > 1) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!warned && std::chrono::steady_clock::now() - waitStart > std::chrono::seconds(1)) {
            logger_->warn("Teardown waiting on {} other reference(s) to the isolate", iso.use_count() - 1);
            warned = true;
        }
    }

    // Everything holding a root or a realm goes before the isolate does.
    std::unordered_map<PendingJob*, std::unique_ptr<PendingJob>> unrun;
    {
        const ub::HandleScope handleScope(*iso);
        {
            std::scoped_lock lock(eventFunctionsMutex_);
            ClearEventFunctionsLocked();
        }
        {
            std::scoped_lock lock(delayedEventMutex_);
            for (auto& evt : delayedEvents_ | std::views::values) {
                evt->Invalidate();
            }
            delayedEvents_.clear();
        }
        {
            std::scoped_lock lock(pendingJobsMutex_);
            unrun = std::exchange(pendingJobs_, {});
        }
        for (const auto& job : unrun | std::views::values) {
            if (auto delayed = std::dynamic_pointer_cast<DelayedEvent>(job->event)) {
                delayed->Invalidate();
            }
        }

        if (inspector_) {
            const ub::ContextScope contextScope(context_);
            inspector_.reset();
        }
        context_.Reset();

        api::classes::ClearAllClassCaches();
    }

    // With the realm and every root gone, nothing the script made is reachable,
    // so collect until every wrapper has been finalized. Both engines collect on
    // request in practice, and finalize during the collection; the bound is for
    // the case where one does not. Jobs are deliberately not pumped: they would
    // run promise continuations in a realm that is being torn down. What is
    // still counted after that is held from outside the heap - a native keeping
    // a root to its own wrapper, or one still owned by another thread - which is
    // a leak worth naming, and it is named here, while the counts still say so:
    // destroying the isolate releases every remaining wrapper regardless.
    auto& tracker = api::InstanceTracker::Instance();
    const auto threadId = std::this_thread::get_id();
    constexpr int32_t MAX_COLLECTION_ROUNDS = 8;
    for (int32_t round = 0; round < MAX_COLLECTION_ROUNDS && !tracker.Snapshot(threadId).empty(); ++round) {
        iso->RequestGarbageCollection();
    }
    for (const auto& [name, count] : tracker.Snapshot(threadId)) {
        logger_->error("Instance leak: {} {} instance(s) not freed", count, name);
    }

    // Releases whatever wrappers the collections above did not, and drops the
    // posted jobs that never ran.
    iso.reset();
    // Tells each event that never ran that it was dropped.
    unrun.clear();

    tracker.ClearThread(threadId);
}

void Script::RunScript() {
    auto iso = isolate_.load();
    if (!iso || context_.IsEmpty()) {
        return;
    }
    const ub::HandleScope handleScope(*iso);
    const ub::ContextScope contextScope(context_);

    ub::TryCatch tryCatch(*iso);

    // Console script with empty path uses a built-in event loop
    if (mode_ == ScriptMode::Console && path_.empty()) {
        auto src = "function main() { print('D2BS :: Started Console'); while(true) { delay(10000); } }";
        if (auto script = runtime::script::CompileSource(context_, src, "Console")) {
            (void)script->Run(context_);
        }
        // Fall through to call main() below
    } else if (!path_.empty()) {
        // Read script file
        std::ifstream file(path_, std::ios::binary);
        if (!file.is_open()) {
            logger_->error("Failed to open script: {}", path_.string());
            return;
        }
        std::string source((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
        file.close();

        // Use the absolute path as the script origin so kolbot's require.js
        // stack-trace regex (matches ".*?d2bs\(kolbot\...)") sees the d2bs\
        // segment of the install path. The base-relative form
        // (ScriptPaths::RelativeScriptPath) is only the display name / URL.
        auto script = runtime::script::CompileSource(context_, std::move(source), path_.string());
        if (!script) {
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                ReportException(tryCatch);
            }
            return;
        }

        // Execute top-level code
        if (!script->Run(context_)) {
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                ReportException(tryCatch);
            }
            return;
        }
    } else {
        // Empty path, non-console - nothing to do
        return;
    }

    // Promise continuations the top level queued run here, when the outermost
    // call returns, rather than at the first delay().
    iso->PumpJobs();

    // Call main() if it exists
    auto mainVal = context_.GlobalObject().Get(context_, "main");
    if (auto mainFn = mainVal ? mainVal->To<ub::Function>() : std::nullopt) {
        // Deserialize arguments passed from load()
        std::vector<ub::Local<ub::Value>> mainArgs;
        for (const auto& argBytes : args_) {
            auto arg = ub::Deserialize(context_, argBytes);
            if (!arg) {
                logger_->error("Failed to deserialize script argument");
                return;
            }
            mainArgs.push_back(*arg);
        }

        if (!mainFn->Call(context_, context_.GlobalObject(), mainArgs)) {
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                ReportException(tryCatch);
            }
        }
        iso->PumpJobs();
    }

    // Populate cached heap stats after initial execution so they're available
    // before the first ExecuteEvents iteration.
    UpdateHeapStats(std::chrono::steady_clock::now());
}

void Script::ReportException(const ub::TryCatch& tryCatch) {
    // Don't log termination exceptions - they're normal during stop()
    if (context_.IsEmpty() || tryCatch.HasTerminated()) {
        return;
    }

    const ub::HandleScope scope(context_.GetIsolate());
    auto message = tryCatch.Message(context_);
    // Also covers a syntax error, which has no stack frame to name a position.
    auto location = tryCatch.Location(context_);
    if (!message && !location) {
        logger_->error("Unknown error");
        return;
    }

    const bool hasFile = location && !location->scriptName.empty();
    logger_->error("{}:{}: {}", hasFile ? location->scriptName : "<unknown>", location ? location->lineNumber : -1,
                   message.value_or("<no message>"));
    if (location && location->sourceLine && !location->sourceLine->empty()) {
        logger_->error("  {}", *location->sourceLine);
    }

    // Reference parity: reference/d2bs/ScriptEngine.cpp:446 - if quitOnError is set
    // and we're in a game, leave the current game so the outer bot loop can
    // recover. Console scripts are exempted: a typo in the live REPL shouldn't
    // kick the user out of their game.
    if (mode_ != ScriptMode::InGame && config::GetAppConfig().quitOnError.load() &&
        game::GetGameState() == game::GameState::InGame) {
        game::ExitGame();
    }
}

std::filesystem::path Script::NormalizePath(const std::filesystem::path& path) {
    auto str = path.string();
    std::ranges::replace(str, '\\', '/');
    return {utils::ToLower(std::move(str))};
}

void Script::UpdateHeapStats(std::chrono::steady_clock::time_point now, bool force) {
    if (!force && now - lastHeapStatsUpdate_ < std::chrono::seconds(1)) {
        return;
    }

    auto iso = isolate_.load();
    if (!iso)
        return;

    cachedHeapStats_.store(std::make_shared<ub::HeapStatistics>(iso->GetHeapStatistics()));
    lastHeapStatsUpdate_ = now;
}

void Script::SetStackCaptureMode(StackCaptureMode mode) {
    const auto prev = stackCaptureMode_.exchange(mode, std::memory_order_acq_rel);
    if (prev == mode) {
        return;
    }
    // Keep the process-wide OnEveryCall tally in sync so OnNativeCall's fast path
    // (skip the per-call script lookup) engages whenever no script is capturing.
    if (mode == StackCaptureMode::OnEveryCall) {
        runtime::script::onEveryCallCaptureCount.fetch_add(1, std::memory_order_relaxed);
    } else if (prev == StackCaptureMode::OnEveryCall) {
        runtime::script::onEveryCallCaptureCount.fetch_sub(1, std::memory_order_relaxed);
    }

    // Switched on from the console: take a first snapshot now rather than at
    // the next yield, which a script spinning in JS may never reach. The
    // interrupt only reads the stack, which is all an interrupt may do.
    if (prev == StackCaptureMode::Off && std::this_thread::get_id() != thread_.get_id()) {
        if (auto iso = isolate_.load()) {
            iso->RequestInterrupt(
                +[](ub::Isolate& isolate, ub::CallbackData /*data*/) {
                    if (auto* script = isolate.GetEmbedderData<Script>()) {
                        script->RefreshLastStackTrace();
                    }
                },
                {});
        }
    }
}

namespace {

// Strip the configured script-base directory off `path` if it appears as a
// prefix. Case-insensitive on Windows, separator-insensitive (treats / and
// \ as equivalent). Paths outside the base pass through unchanged.
[[nodiscard]] std::string TrimScriptBase(std::string_view path, const std::string& baseStr) {
    if (path.empty() || baseStr.empty() || path.size() < baseStr.size()) {
        return std::string{path};
    }
    auto fold = [](char c) -> char {
        if (c == '\\') {
            return '/';
        }
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (size_t i = 0; i < baseStr.size(); ++i) {
        if (fold(path[i]) != fold(baseStr[i])) {
            return std::string{path};
        }
    }
    // Prefix matched. Require the next character (if any) to be a separator
    // so we don't mis-strip "C:\foo\bar" when base is "C:\foo\b".
    if (path.size() > baseStr.size()) {
        const char next = path[baseStr.size()];
        if (next != '/' && next != '\\') {
            return std::string{path};
        }
        return std::string{path.substr(baseStr.size() + 1)};
    }
    return std::string{path.substr(baseStr.size())};
}

}  // namespace

void Script::RequestGarbageCollection() const {
    auto iso = isolate_.load();
    if (!iso) {
        return;
    }
    // Same-thread: ask directly - an interrupt requested from here would wait
    // for the next checkpoint inside running script.
    if (std::this_thread::get_id() == thread_.get_id()) {
        iso->RequestGarbageCollection();
        return;
    }
    // Cross-thread: schedule on the isolate's own thread. The callback is
    // handed the isolate, so nothing here depends on `this` still being alive
    // when the interrupt eventually fires.
    iso->RequestInterrupt(+[](ub::Isolate& isolate, ub::CallbackData /*data*/) { isolate.RequestGarbageCollection(); },
                          {});
}

void Script::RefreshLastStackTrace(int32_t maxFrames) {
    auto iso = isolate_.load();
    if (!iso) {
        return;
    }

    const auto trace = ub::CaptureStackFrames(*iso, static_cast<uint32_t>(std::max(maxFrames, 0)));
    const std::string baseStr = config::GetAppConfig().GetScriptPaths().basePath.string();

    std::vector<StackFrame> frames;
    frames.reserve(trace.size());
    for (const auto& frame : trace) {
        frames.push_back({.functionName = frame.functionName,
                          // Trim base path so stack traces show e.g. "libs/common/Town.js" not the full install path.
                          .scriptName = TrimScriptBase(frame.scriptName, baseStr),
                          .line = frame.lineNumber,
                          .column = frame.columnNumber});
    }

    auto snapshot = std::make_shared<StackTraceSnapshot>();
    snapshot->frames = std::move(frames);
    lastStackTrace_.store(std::move(snapshot));
}

// ============================================================================
// Event System
// ============================================================================

void Script::RegisterEvent(const std::string& eventName, const ub::Local<ub::Function>& func) {
    if (eventName.empty())
        return;
    auto iso = isolate_.load();
    if (!iso)
        return;
    std::scoped_lock lock(eventFunctionsMutex_);
    // Published before the insert: dispatchers read the count without this mutex, so bumping it
    // after would leave a window where the handler exists but the probe still reads zero.
    events::ListenerCount::For(eventName).Add(1);
    eventFunctions_[eventName].emplace_back(*iso, func);
}

void Script::UnregisterEvent(const std::string& eventName, const ub::Local<ub::Function>& func) {
    if (eventName.empty())
        return;
    std::scoped_lock lock(eventFunctionsMutex_);
    auto it = eventFunctions_.find(eventName);
    if (it == eventFunctions_.end())
        return;

    const auto removed = std::erase_if(
        it->second, [&func](const ub::Global<ub::Function>& function) { return function.StrictEquals(func); });
    events::ListenerCount::For(eventName).Add(-static_cast<int32_t>(removed));
    if (it->second.empty()) {
        eventFunctions_.erase(it);
    }
}

bool Script::IsEventRegistered(std::string_view eventName) {
    std::scoped_lock lock(eventFunctionsMutex_);
    auto it = eventFunctions_.find(std::string(eventName));
    return it != eventFunctions_.end() && !it->second.empty();
}

void Script::ClearEvent(const std::string& eventName) {
    if (eventName.empty())
        return;
    std::scoped_lock lock(eventFunctionsMutex_);
    if (auto node = eventFunctions_.extract(eventName); !node.empty()) {
        events::ListenerCount::For(eventName).Add(-static_cast<int32_t>(node.mapped().size()));
    }
}

void Script::ClearAllEvents() {
    std::scoped_lock lock(eventFunctionsMutex_);
    ClearEventFunctionsLocked();
}

void Script::ClearEventFunctionsLocked() {
    // Remove first, then decrement - the reverse order would publish a zero count while the
    // handlers are still installed, and a dispatch in that window would skip the event outright.
    auto removed = std::exchange(eventFunctions_, {});
    for (const auto& [name, fns] : removed) {
        events::ListenerCount::For(name).Add(-static_cast<int32_t>(fns.size()));
    }
}

void Script::AddDelayedEvent(const std::shared_ptr<DelayedEvent>& event) {
    std::scoped_lock lock(delayedEventMutex_);
    delayedEvents_[event->EventId()] = event;
}

bool Script::RemoveDelayedEvent(uint32_t eventId) {
    std::scoped_lock lock(delayedEventMutex_);
    auto node = delayedEvents_.extract(eventId);
    if (!node.empty()) {
        node.mapped()->Cancel();
        return true;
    }
    return false;
}

void Script::ExecuteEvents(std::chrono::milliseconds duration) {
    auto iso = isolate_.load();
    if (!iso)
        return;
    auto stopToken = thread_.get_stop_token();

    // Idle-wait granularity (INI IdleSleepIntervalMs): wall-ms slept per idle pass.
    const auto idleSleep = config::GetAppConfig().idleSleepInterval;

    // Reached from a handler (delay() inside a callback): hand the handler phase back on exit.
    const bool nested = idle_.InPhase();

    // When paused, sleep without processing events.
    while (state_.load() == ScriptState::Paused && !stopToken.stop_requested()) {
        idle_.Enter(IdlePhase::Paused);
        speedhack::SpeedhackDisabledScope realWaits;
        std::this_thread::sleep_for(idleSleep);
    }

    const auto deadline = std::chrono::steady_clock::now() + duration;
    const float speed = speedhack::GetSpeed();
    while (true) {
        idle_.Enter(IdlePhase::Pump);

        // Promise continuations, then posted events and timers that have fallen due.
        iso->PumpJobs();

        // Handle whatever the debugger client has sent, on the isolate thread
        // alongside the engine's own queue.
        if (inspector_) {
            inspector_->DrainIncoming();
        }

        // steady_clock::now() is QueryPerformanceCounter on MSVC, so each read
        // routes through the speedhack hook: take one reading per pass and reuse it
        // for the heap-stat throttle, the deadline check, and the wait.
        const auto now = std::chrono::steady_clock::now();

        // Periodically cache heap stats for cross-thread readers.
        // GetHeapStatistics iterates all heap spaces (not cheap), so throttle.
        UpdateHeapStats(now);

        if (now >= deadline || state_.load() != ScriptState::Running || stopToken.stop_requested() ||
            (mode_ == ScriptMode::InGame && game::GetGameState() != game::GameState::InGame)) {
            break;
        }

        // deadline/now are scaled "virtual" time; convert the remaining budget to
        // real wall time, then sleep (rather than busy-yielding the sub-idleSleep
        // tail) so the core is released. Cap the nap at the idle granularity so a
        // cross-thread event post is serviced within idleSleep.
        const auto realRemaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double, std::milli>(deadline - now) / speed);
        idle_.Enter(IdlePhase::Wait);
        speedhack::SpeedhackDisabledScope realWaits;
        std::this_thread::sleep_for(std::clamp(realRemaining, std::chrono::milliseconds(1), idleSleep));
    }
    if (nested) {
        idle_.Resume(IdlePhase::Handlers);
    } else {
        idle_.Leave();
    }
}

bool Script::ExecuteEvent(const std::shared_ptr<BaseEvent>& event) {
    auto iso = isolate_.load();
    if (!iso)
        return false;

    if (thread_.get_id() == std::this_thread::get_id()) {
        if (context_.IsEmpty()) {
            return false;
        }
        // Same thread - execute synchronously. A posted job runs outside any
        // call, so the context is entered here rather than assumed.
        const ub::HandleScope scope(*iso);
        const ub::ContextScope contextScope(context_);
        std::vector<ub::Local<ub::Function>> fns;
        {
            // Copy handlers out from under the lock to avoid re-entrance deadlocks
            std::scoped_lock lock(eventFunctionsMutex_);
            auto it = eventFunctions_.find(std::string(event->Name()));
            if (it != eventFunctions_.end()) {
                fns.reserve(it->second.size());
                for (auto& globalFn : it->second) {
                    fns.push_back(globalFn.Get(*iso));
                }
            }
        }

        // Always call Execute - even with empty fns, BlockableEvent needs to
        // decrement its remaining_ counter to avoid stalling the game thread.
        {
            // Pumped from inside delay(): the handler is the script's work, not delay's.
            const auto phase = idle_.Nest(IdlePhase::Handlers);
            const profiling::ScopedNativeExclusion handlerIsJs;
            event->Execute(context_, fns);
        }

        // Re-post interval timers AFTER execution to prevent unbounded accumulation.
        // If the callback takes longer than repeatMs, the next firing is deferred
        // rather than queued concurrently. This trades fixed-rate timing for safety
        // (timer drift is acceptable).
        if (auto delayedEvent = std::dynamic_pointer_cast<DelayedEvent>(event); delayedEvent) {
            if (delayedEvent->RepeatMs() > 0 && !delayedEvent->IsCancelled()) {
                PostEvent(delayedEvent, delayedEvent->RepeatMs());
            } else {
                RemoveDelayedEvent(delayedEvent->EventId());
            }
        }
        return true;
    }

    // Different thread - post it as a job on the script's isolate.
    // The job is run by PumpJobs during delay() calls.
    return PostEvent(event);
}

Script::PendingJob::~PendingJob() {
    // Keeps BlockableEvent::remaining_ honest when the event is dropped unrun
    // or the isolate tears down mid-dispatch.
    if (!isDelivered) {
        event->OnDropped();
    }
}

void Script::RunPendingJob(ub::Isolate& isolate, ub::CallbackData data) {
    auto* script = isolate.GetEmbedderData<Script>();
    auto* key = data.As<PendingJob>();
    if (script == nullptr || key == nullptr) {
        return;
    }
    std::unique_ptr<PendingJob> job;
    {
        std::scoped_lock lock(script->pendingJobsMutex_);
        auto node = script->pendingJobs_.extract(key);
        if (node.empty()) {
            return;
        }
        job = std::move(node.mapped());
    }
    job->isDelivered = script->ExecuteEvent(job->event);
}

bool Script::PostEvent(const std::shared_ptr<BaseEvent>& event, uint32_t delayMs) {
    // Take a shared_ptr copy - keeps the isolate alive for the duration of this
    // call, even if TeardownIsolate runs concurrently on the script thread.
    auto iso = isolate_.load();
    if (!iso)
        return false;

    // Reject events if the script is shutting down.
    auto currentState = GetState();
    if (currentState == ScriptState::Stopping || currentState == ScriptState::Stopped) {
        return false;
    }

    // Registered before it is posted, and while `iso` is held: TeardownIsolate
    // waits for this copy to drop before collecting what never ran.
    auto job = std::make_unique<PendingJob>(event);
    auto* key = job.get();
    {
        std::scoped_lock lock(pendingJobsMutex_);
        pendingJobs_.emplace(key, std::move(job));
    }
    if (!iso->PostDelayedJob(&Script::RunPendingJob, ub::CallbackData::For(*key),
                             static_cast<double>(delayMs) / 1000.0)) {
        std::scoped_lock lock(pendingJobsMutex_);
        pendingJobs_.erase(key);
        return false;
    }
    return true;
}

// ============================================================================
// Include System
// ============================================================================

bool Script::IsIncluded(const std::filesystem::path& absolutePath) const {
    return includes_.contains(NormalizePath(absolutePath));
}

bool Script::Include(const std::filesystem::path& absolutePath) {
    auto normalized = NormalizePath(absolutePath);

    // Skip already included, in-progress, and self-inclusion
    if (includes_.contains(normalized) || inProgressIncludes_.contains(normalized) || normalized == normalizedPath_) {
        return true;
    }

    auto iso = isolate_.load();
    if (!iso || context_.IsEmpty())
        return false;

    ub::TryCatch tryCatch(*iso);

    // Read file
    std::ifstream file(absolutePath, std::ios::binary);
    if (!file.is_open()) {
        logger_->warn("Failed to open include: {}", absolutePath.string());
        return false;
    }
    std::string source((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
    file.close();

    // Match RunScript: absolute path so kolbot's require.js stack-trace regex finds the d2bs\ segment.
    auto script = runtime::script::CompileSource(context_, std::move(source), absolutePath.string());
    if (!script) {
        logger_->warn("Failed to compile include: {}", absolutePath.string());
        if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
            ReportException(tryCatch);
            tryCatch.ReThrow();
        }
        return false;
    }

    // Execute
    inProgressIncludes_.emplace(normalized);

    if (script->Run(context_)) {
        includes_.emplace(normalized);
    } else {
        logger_->warn("Failed to execute include: {}", absolutePath.string());
    }

    inProgressIncludes_.erase(normalized);

    if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
        ReportException(tryCatch);
        tryCatch.ReThrow();
    }

    return includes_.contains(normalized);
}

// ============================================================================
// Drawables (screen hooks)
// ============================================================================

void Script::AddDrawable(std::shared_ptr<runtime::drawing::Drawable> drawable) {
    if (!drawable) {
        return;
    }
    std::unique_lock lock(drawablesMutex_);
    drawables_.push_back(std::move(drawable));
}

void Script::RemoveDrawable(const std::shared_ptr<runtime::drawing::Drawable>& drawable, bool fireLeaveEvent) {
    if (!drawable) {
        return;
    }

    // Run onDestroy on the script thread - that keeps the InstanceTracker
    // bucket aligned with the Increment thread. Dispatch the leave event after
    // releasing the lock so a JS callback can safely re-enter
    // Add/RemoveDrawable without self-deadlock, and drop the handler entry only
    // once that dispatch has resolved it: the event looks the handler up when it
    // runs rather than carrying it. This is always the script's own thread, so
    // ExecuteEvent runs the leave handler synchronously here.
    bool fireLeave = false;
    {
        std::unique_lock lock(drawablesMutex_);
        auto it = std::ranges::find(drawables_, drawable);
        if (it == drawables_.end()) {
            return;
        }
        drawables_.erase(it);

        auto handlers = drawableHandlers_.find(drawable.get());
        fireLeave = fireLeaveEvent && drawable->isHovered.load() && isolate_.load() &&
                    handlers != drawableHandlers_.end() && !handlers->second.hover.IsEmpty();
        drawable->hasClick.store(false);
        drawable->hasHover.store(false);
        if (drawable->onDestroy) {
            drawable->onDestroy();
            drawable->onDestroy = nullptr;
        }
    }
    if (fireLeave) {
        ExecuteEvent(std::make_shared<ScreenHookHoverEvent>(drawable, game::Point::Zero, false));
    }
    std::unique_lock lock(drawablesMutex_);
    drawableHandlers_.erase(drawable.get());
}

std::vector<std::shared_ptr<runtime::drawing::Drawable>> Script::GetDrawables() {
    std::shared_lock lock(drawablesMutex_);
    return drawables_;
}

void Script::SetDrawableHandler(runtime::drawing::Drawable& drawable, DrawableHandler which,
                                const ub::Local<ub::Function>& handler) {
    std::unique_lock lock(drawablesMutex_);
    auto iso = isolate_.load();
    const bool isInstalled = !handler.IsEmpty() && iso;
    if (isInstalled) {
        auto& slot = drawableHandlers_[&drawable];
        (which == DrawableHandler::Click ? slot.click : slot.hover) = ub::Global<ub::Function>(*iso, handler);
    } else if (auto it = drawableHandlers_.find(&drawable); it != drawableHandlers_.end()) {
        (which == DrawableHandler::Click ? it->second.click : it->second.hover).Reset();
    }
    (which == DrawableHandler::Click ? drawable.hasClick : drawable.hasHover).store(isInstalled);
}

std::optional<ub::Local<ub::Function>> Script::GetDrawableHandler(const runtime::drawing::Drawable& drawable,
                                                                  DrawableHandler which) {
    std::shared_lock lock(drawablesMutex_);
    auto it = drawableHandlers_.find(&drawable);
    auto iso = isolate_.load();
    if (it == drawableHandlers_.end() || !iso) {
        return std::nullopt;
    }
    const auto& slot = (which == DrawableHandler::Click) ? it->second.click : it->second.hover;
    if (slot.IsEmpty()) {
        return std::nullopt;
    }
    return slot.Get(*iso);
}

bool Script::DispatchDrawableClick(std::shared_ptr<const runtime::drawing::Drawable> drawable, game::ClickButton button,
                                   game::Point pos) {
    if (!drawable || !drawable->hasClick.load() || !IsAlive()) {
        return false;
    }
    auto evt = std::make_shared<ScreenHookClickEvent>(std::move(drawable), button, pos);
    // Click events are blockable; bump the expected-handler counter so
    // IsBlocked() waits for the JS callback's return value. Hover events are
    // fire-and-forget and do not use this counter.
    evt->IncrementExpected();
    if (!ExecuteEvent(evt)) {
        evt->DecrementExpected();
        return false;
    }
    return evt->IsBlocked(std::chrono::seconds(3)).value_or(false);
}

void Script::DispatchDrawableHover(std::shared_ptr<const runtime::drawing::Drawable> drawable, game::Point pos,
                                   bool entered) {
    if (!drawable || !drawable->hasHover.load() || !IsAlive()) {
        return;
    }
    ExecuteEvent(std::make_shared<ScreenHookHoverEvent>(std::move(drawable), pos, entered));
}

}  // namespace d2bs
