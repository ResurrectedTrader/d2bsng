#include "Script.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <ranges>
#include <thread>
#include <vector>

#include "api/classes/ClassRegistry.h"
#include "api/core/V8Convert.h"
#include "api/core/V8InstanceTracker.h"
#include "api/globals/Constants.h"
#include "api/globals/CoreFunctions.h"
#include "api/globals/GameFunctions.h"
#include "api/globals/HashFunctions.h"
#include "api/globals/MenuFunctions.h"
#include "components/config/AppConfig.h"
#include "components/drawing/Drawable.h"
#include "components/events/BaseEvent.h"
#include "components/events/DelayedEvent.h"
#include "components/events/Events.h"
#include "components/inspector/ScriptInspector.h"
#include "components/script/CompileSource.h"
#include "components/script/ScriptEngine.h"
#include "components/speedhack/Speedhack.h"
#include "components/v8/V8Host.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "utils/DeferGuard.h"
#include "utils/utils.h"

namespace d2bs {

Script::Script(std::filesystem::path path, ScriptMode mode, std::vector<std::vector<uint8_t>> args)
    : path_(std::move(path)),
      normalizedPath_(NormalizePath(path_)),
      mode_(mode),
      args_(std::move(args)),
      logger_(d2bs::utils::GetLogger(path_.filename().string())) {}

std::shared_ptr<spdlog::logger> GetLogger(v8::Isolate* isolate) {
    if (auto* script = ScriptEngine::Instance().GetScript(isolate)) {
        return script->GetLogger();
    }
    return utils::GetLogger("js");
}

// NOLINTNEXTLINE(bugprone-exception-escape) - join() can throw std::system_error; terminate is fine at teardown
Script::~Script() {
    Stop();
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
        d2bs::game::GameWriteLockReleaser writeReleaser;
        d2bs::game::GameReadLockReleaser readReleaser;
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

v8::Local<v8::Context> Script::GetContext() const {
    // Isolate-thread-only: the TryGetCurrent() fallback below resolves the context
    // against whatever isolate is current, which is correct only on this script's
    // own thread. Debug guard for the contract documented on the declaration.
    assert(std::this_thread::get_id() == thread_.get_id() && "GetContext is isolate-thread-only");
    if (context_.IsEmpty()) {
        return {};
    }
    auto iso = isolate_.load();
    // TeardownIsolate exchanges isolate_ to null before resetting context_, yet
    // the isolate is still entered (Isolate::Scope active) while ~ScriptInspector
    // reads the context here for contextDestroyed. Fall back to the current
    // isolate so that path still resolves the live context.
    v8::Isolate* raw = iso ? iso.get() : v8::Isolate::TryGetCurrent();
    if (!raw) {
        return {};
    }
    return context_.Get(raw);
}

void Script::Evaluate(const std::string& code) {
    auto event = std::make_shared<EvaluateEvent>(code);
    ExecuteEvent(event);
}

void Script::AttachInspector() {
    auto iso = isolate_.load();
    if (!iso) {
        return;
    }
    v8::Isolate::Scope isolateScope(iso.get());
    v8::HandleScope handleScope(iso.get());
    if (GetContext().IsEmpty()) {
        return;
    }
    // Label shown in chrome://inspect. Prefix with the active profile (when set)
    // so multi-box users can tell which bot a target belongs to. The console
    // script carries a real path (its lookup fallback), so classify by mode.
    const bool isConsole = mode_ == ScriptMode::Console;
    std::string name = isConsole ? std::string("Console") : GetName();
    const std::string profile = config::GetAppConfig().GetProfileName();
    std::string title = profile.empty() ? name : (profile + " / " + name);
    // The url is the base-relative path as a file:// URL, like every script URL
    // DevTools sees (resourceNameToUrl maps script origins the same way): the
    // install path stays out of DevTools and the URLs are stable across machines.
    std::string url =
        isConsole ? std::string("d2bs://console") : config::GetAppConfig().GetScriptPaths().FileUrl(path_);
    inspector_ = std::make_unique<framework::inspector::ScriptInspector>(this, std::move(title), std::move(url));
}

void Script::ThreadMain(const std::stop_token& stopToken) {
    // Keep ourselves alive for the entire duration of ThreadMain.
    // RemoveSelfFromEngine() drops the engine map's shared_ptr, which may be the
    // last external reference.  Without this, `this` is destroyed mid-function.
    auto self = shared_from_this();

    // V8 requires per-thread sandbox preparation before creating isolates
    v8::SandboxHardwareSupport::PrepareCurrentThreadForHardwareSandboxing();

    // Store native Win32 thread ID for JS threadid property (avoids hash truncation)
    nativeThreadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);

    d2bs::thread_utils::SetThreadDescription(GetName());

    // Script threads run user JS; their Date.now / delay / setTimeout
    // should observe the global time multiplier. V8's internal worker
    // pool runs on threads we never touch, so those stay on real time.
    d2bs::speedhack::OptInCurrentThread();

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
bool InstallConsoleRouting(v8::Isolate* iso, v8::Local<v8::Context> context) {
    // Native flag-reader the shim closes over: true while the inspector is
    // running a REPL evaluate (set around dispatchProtocolMessage).
    auto evaluating = v8::Function::New(
                          context,
                          +[](const v8::FunctionCallbackInfo<v8::Value>& info) {
                              info.GetReturnValue().Set(framework::inspector::ScriptInspector::IsEvaluating());
                          })
                          .ToLocalChecked();
    if (context->Global()
            ->Set(context, api::v8_convert::ToV8(iso, "__d2bsInspectorEvaluating"), evaluating)
            .IsNothing()) {
        return false;
    }
    // Stash V8's built-in (inspector-wired) console, then redefine `console` as
    // an accessor: a REPL evaluate gets V8's console (so console.log lands in the
    // DevTools Console panel), while normal script execution gets whatever
    // console the script installs - captured by the setter. kolbot's
    // `global.console = global.console || polyfill()` reads undefined at script
    // time (depth 0) and installs its print-routed polyfill, which the setter
    // captures. The native helper is deleted once the shim closes over it.
    static constexpr std::string_view CONSOLE_SHIM = R"JS(
(function () {
  const v8console = globalThis.console;
  let scriptConsole;
  const inspectorEvaluating = globalThis.__d2bsInspectorEvaluating;
  delete globalThis.__d2bsInspectorEvaluating;
  Object.defineProperty(globalThis, 'console', {
    configurable: true,
    get() { return inspectorEvaluating() ? v8console : scriptConsole; },
    set(v) { scriptConsole = v; },
  });
})();
)JS";
    v8::Local<v8::Script> shim;
    if (!v8::Script::Compile(context, api::v8_convert::ToV8(iso, CONSOLE_SHIM).As<v8::String>()).ToLocal(&shim)) {
        return false;
    }
    return !shim->Run(context).IsEmpty();
}

}  // namespace

void Script::SetupIsolate() {
    // Ensure V8 platform is initialized (singleton)
    (void)V8Host::GetPlatform();

    auto allocator = std::shared_ptr<v8::ArrayBuffer::Allocator>(v8::ArrayBuffer::Allocator::NewDefaultAllocator());

    v8::Isolate::CreateParams createParams;
    createParams.array_buffer_allocator = allocator.get();

    auto& appConfig = config::GetAppConfig();
    if (appConfig.memoryLimit > 0) {
        createParams.constraints.set_max_old_generation_size_in_bytes(appConfig.memoryLimit);
    }

    auto* iso = v8::Isolate::New(createParams);

    // V8 calls these on internal CHECK/DCHECK failures and OOM. Default action
    // is OS::Abort which bypasses our SetUnhandledExceptionFilter - wire them
    // through spdlog::critical so the crash lands in the console, dump a
    // crash log next to Game.exe, then exit with a distinctive code.
    iso->SetFatalErrorHandler(+[](const char* location, const char* message) {
        auto dump = std::format("V8 fatal error at '{}': {}\n{}\n", location ? location : "<null>",
                                message ? message : "<null>", d2bs::thread_utils::GetThreadStacktrace());
        d2bs::thread_utils::CrashAndExit(dump, 0xD2B50001);
    });
    iso->SetOOMErrorHandler(+[](const char* location, const v8::OOMDetails& details) {
        auto dump = std::format("V8 OOM at '{}': {} (heap_oom={})\n{}\n", location ? location : "<null>",
                                details.detail ? details.detail : "<null>", details.is_heap_oom,
                                d2bs::thread_utils::GetThreadStacktrace());
        d2bs::thread_utils::CrashAndExit(dump, 0xD2B50002);
    });

    // Wrap in shared_ptr with a custom deleter that captures the allocator,
    // guaranteeing it outlives Dispose().  The raw pointer `iso` remains valid
    // for the rest of this function (and RunScript/TeardownIsolate on the same
    // thread) because the shared_ptr stored in isolate_ keeps it alive.
    // Cross-thread callers (Stop, PostEvent) load their own shared_ptr copy,
    // which prevents Dispose from running until they're done.
    //
    // The deleter also performs the V8InstanceTracker leak check.  The check
    // must run *after* Dispose because the JS heap's roots - compilation
    // cache, microtask queue, queued tasks - are only fully released by
    // Dispose; pre-Dispose checks see those objects as alive even though V8
    // will free them moments later.  threadId captures the script's own
    // thread (we're on it now); cross-thread holders of the shared_ptr won't
    // fire this callback until they drop their copy.
    auto threadId = std::this_thread::get_id();
    auto logger = logger_;
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks) - false positive: allocator captured by shared_ptr deleter
    isolate_.store(
        std::shared_ptr<v8::Isolate>(iso, [alloc = std::move(allocator), threadId, logger](v8::Isolate* ptr) {
            v8::platform::NotifyIsolateShutdown(V8Host::GetPlatform(), ptr);
            ptr->Dispose();
            // alloc destroyed here - allocator guaranteed to outlive Dispose()

            auto& tracker = api::V8InstanceTracker::Instance();
            auto remaining = tracker.Snapshot(threadId);
            for (const auto& [name, count] : remaining) {
                logger->error("Instance leak: {} {} instance(s) not freed", count, name);
            }
            tracker.ClearThread(threadId);
        }));
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)

    v8::Isolate::Scope isolateScope(iso);
    v8::HandleScope handleScope(iso);

    // Create global object template
    auto global = v8::ObjectTemplate::New(iso);

    // Register all class constructors (Unit, Room, File, etc.)
    api::classes::RegisterAllClasses(iso, global);

    // Register all global functions
    api::globals::RegisterCoreFunctions(iso, global);
    api::globals::RegisterGameFunctions(iso, global);
    api::globals::RegisterMenuFunctions(iso, global);
    api::globals::RegisterHashFunctions(iso, global);

    // Register global constants (FILE_READ, FILE_WRITE, FILE_APPEND)
    api::globals::RegisterConstants(iso, global);

    // Create context with the configured global template
    auto context = v8::Context::New(iso, nullptr, global);
    context_.Reset(iso, context);

    // Store Script pointer in isolate slot for ScriptEngine::GetScript()
    iso->SetData(0, this);

    // Always register this isolate as a Chrome DevTools target. Done before the
    // console shim below so that shim captures V8's inspector-wired console for
    // the DevTools REPL path. Attaching has no measurable overhead until a client
    // connects, and the InspectorServer only exposes the target when running
    // (toggled by inspectorPort's sign via the Settings panel / SetInspector).
    AttachInspector();

    // Create the 'me' global object (player unit with extra properties)
    {
        v8::Context::Scope contextScope(context);

        // Route `console` by caller. V8 installs a built-in `console` on every
        // context, wired to the inspector. Kolbot's Polyfill.js installs its own
        // print-routed console via `global.console = global.console || (...)()`,
        // which only fires if `console` reads falsy at script time. We want both:
        // a script's console.log -> the script's polyfill (d2bs console), and
        // console.log typed in the DevTools console -> V8's console (DevTools
        // panel). Since both share one global, InstallConsoleRouting makes
        // `console` an accessor that returns V8's console while the inspector is
        // evaluating (ScriptInspector::IsEvaluating, set around
        // dispatchProtocolMessage) and the script's console otherwise. See
        // docs/inspector.md.
        if (!InstallConsoleRouting(iso, context)) {
            logger_->error("Failed to install console routing");
            return;
        }

        auto me = api::classes::CreateMeObject(iso, context);
        if (me.IsEmpty()) {
            logger_->error("Failed to create 'me' global object");
            return;
        }
        context->Global()->Set(context, api::v8_convert::ToV8(iso, "me"), me).Check();
        framework::script::ApplyCompatibilityPrelude(iso, context);
    }
}

void Script::TeardownIsolate() {
    // RemoveDrawable handles per-drawable cleanup (pre-Reset Globals, run
    // onDestroy on this thread). fireLeaveEvent=false skips the synchronous
    // hover-leave dispatch - the script's event loop has exited and a same-
    // thread ExecuteEvent would run user JS that's about to be torn down.
    for (const auto& drawable : GetDrawables()) {
        RemoveDrawable(drawable, /*fireLeaveEvent=*/false);
    }
    // isolate_ has its own atomic sync - no need to cover the exchange with
    // drawablesMutex_. Cross-thread callers (Stop/PostEvent) racing the
    // exchange see nullptr post-swap and bail out.
    auto iso = isolate_.exchange(std::shared_ptr<v8::Isolate>());
    if (!iso) {
        return;
    }
    auto* raw = iso.get();

    // Enter the isolate before destroying v8::Global handles - their destructors
    // and the class cache cleanup require the isolate to be current.
    {
        v8::Isolate::Scope isolateScope(raw);
        v8::HandleScope handleScope(raw);

        // Clear all events before disposing isolate (releases v8::Global handles)
        {
            std::scoped_lock lock(eventFunctionsMutex_);
            eventFunctions_.clear();
        }
        {
            std::scoped_lock lock(delayedEventMutex_);
            for (auto& evt : delayedEvents_ | std::views::values) {
                evt->Invalidate();
            }
            delayedEvents_.clear();
        }

        // Tear down the inspector while the isolate and context are still alive
        // (V8Inspector::contextDestroyed needs the live context).
        inspector_.reset();

        context_.Reset();

        // Clear per-isolate template caches before disposing
        api::classes::ClearAllClassCaches(raw);

        // Top-level context with no nested contexts - kNoDependants lets V8
        // be more aggressive about reclaiming the context.
        raw->ContextDisposedNotification(v8::ContextDependants::kNoDependants);

        // Encourage V8 to run weak callbacks before Dispose - anything
        // freed here saves work; anything still pinned (V8 compilation
        // cache, queued tasks) is released by Dispose and reported by the
        // post-Dispose leak check installed in SetupIsolate's deleter.
        RequestGarbageCollection();
        raw->LowMemoryNotification();
    }
    // Isolate::Scope exited - IsInUse() is now false.
    // `iso` drops here.  If Stop() on another thread still holds a copy, the
    // custom deleter (NotifyIsolateShutdown + Dispose) is deferred until that
    // copy drops - both are safe from any thread.
}

void Script::RunScript() {
    auto* iso = isolate_.load().get();
    v8::Isolate::Scope isolateScope(iso);
    v8::HandleScope handleScope(iso);
    auto context = GetContext();
    v8::Context::Scope contextScope(context);

    v8::TryCatch tryCatch(iso);

    // Console script with empty path uses a built-in event loop
    if (mode_ == ScriptMode::Console && path_.empty()) {
        auto src = "function main() { print('D2BS :: Started Console'); while(true) { delay(10000); } }";
        v8::Local<v8::Script> script;
        if (framework::script::CompileSource(iso, context, src, "Console").ToLocal(&script)) {
            v8::Local<v8::Value> dummy;
            (void)script->Run(context).ToLocal(&dummy);
        }
        // Fall through to call main() below
    } else if (!path_.empty()) {
        // Read script file
        std::ifstream file(path_, std::ios::binary);
        if (!file.is_open()) {
            logger_->error("Failed to open script: {}", path_.string());
            return;
        }
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Use the absolute path as the V8 origin so kolbot's require.js
        // stack-trace regex (matches ".*?d2bs\(kolbot\...)") sees the d2bs\
        // segment of the install path. The base-relative form
        // (ScriptPaths::RelativeScriptPath) is only the display name / URL.
        v8::Local<v8::Script> script;
        if (!framework::script::CompileSource(iso, context, std::move(source), path_.string()).ToLocal(&script)) {
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                ReportException(tryCatch);
            }
            return;
        }

        // Execute top-level code
        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                ReportException(tryCatch);
            }
            return;
        }
    } else {
        // Empty path, non-console - nothing to do
        return;
    }

    // Call main() if it exists
    auto mainStr = api::v8_convert::ToV8(iso, "main");
    v8::Local<v8::Value> mainVal;
    if (context->Global()->Get(context, mainStr).ToLocal(&mainVal) && mainVal->IsFunction()) {
        auto mainFn = mainVal.As<v8::Function>();

        // Deserialize arguments passed from load()
        std::vector<v8::Local<v8::Value>> mainArgs;
        for (const auto& argBytes : args_) {
            auto deserializer = v8::ValueDeserializer(iso, argBytes.data(), argBytes.size());
            if (deserializer.ReadHeader(context).FromMaybe(false)) {
                v8::Local<v8::Value> arg;
                if (deserializer.ReadValue(context).ToLocal(&arg)) {
                    mainArgs.push_back(arg);
                } else {
                    logger_->error("Failed to deserialize script argument");
                    return;
                }
            } else {
                logger_->error("Failed to read script argument header");
                return;
            }
        }

        v8::Local<v8::Value> mainResult;
        if (!mainFn
                 ->Call(context, context->Global(), static_cast<int32_t>(mainArgs.size()),
                        mainArgs.empty() ? nullptr : mainArgs.data())
                 .ToLocal(&mainResult)) {
            if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
                ReportException(tryCatch);
            }
        }
    }

    // Populate cached heap stats after initial execution so they're available
    // before the first ExecuteEvents iteration.
    UpdateHeapStats();
}

void Script::ReportException(v8::TryCatch& tryCatch) {
    auto* iso = isolate_.load().get();
    if (!iso)
        return;

    // Don't log termination exceptions - they're normal during stop()
    if (tryCatch.HasTerminated())
        return;

    v8::HandleScope scope(iso);
    auto message = tryCatch.Message();
    if (message.IsEmpty()) {
        logger_->error("Unknown error");
        return;
    }

    v8::String::Utf8Value errorStr(iso, message->Get());
    auto context = GetContext();
    int32_t lineNum = message->GetLineNumber(context).FromMaybe(-1);
    v8::String::Utf8Value fileName(iso, message->GetScriptResourceName());

    logger_->error("{}:{}: {}", *fileName ? *fileName : "<unknown>", lineNum, *errorStr ? *errorStr : "<no message>");

    v8::Local<v8::String> sourceLine;
    if (message->GetSourceLine(context).ToLocal(&sourceLine)) {
        v8::String::Utf8Value lineStr(iso, sourceLine);
        if (*lineStr) {
            logger_->error("  {}", *lineStr);
        }
    }

    // Reference parity: reference/d2bs/ScriptEngine.cpp:446 - if quitOnError is set
    // and we're in a game, leave the current game so the outer bot loop can
    // recover. Console scripts are exempted: a typo in the live REPL shouldn't
    // kick the user out of their game.
    if (mode_ != ScriptMode::InGame && d2bs::config::GetAppConfig().quitOnError.load() &&
        d2bs::game::GetGameState() == d2bs::game::GameState::InGame) {
        d2bs::game::ExitGame();
    }
}

std::filesystem::path Script::NormalizePath(const std::filesystem::path& path) {
    auto str = path.string();
    std::ranges::replace(str, '\\', '/');
    return {d2bs::utils::ToLower(std::move(str))};
}

void Script::UpdateHeapStats(bool force) {
    auto now = std::chrono::steady_clock::now();
    if (!force && now - lastHeapStatsUpdate_ < std::chrono::seconds(1)) {
        return;
    }

    auto* iso = isolate_.load().get();
    if (!iso)
        return;

    auto stats = std::make_shared<v8::HeapStatistics>();
    iso->GetHeapStatistics(stats.get());
    cachedHeapStats_.store(std::move(stats));
    lastHeapStatsUpdate_ = now;
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
    // Same-thread fast path. TeardownIsolate calls into here for its leak-
    // detection loop; on the script's own thread the interrupt would just
    // queue (we're past the event loop) and never fire.
    if (std::this_thread::get_id() == thread_.get_id()) {
        iso->MemoryPressureNotification(v8::MemoryPressureLevel::kCritical);
        iso->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
        return;
    }
    // Cross-thread: schedule on the isolate's own thread. The callback
    // re-resolves the Script via ScriptEngine::GetScript(iso) so we don't
    // depend on `this` still being alive when the interrupt eventually fires.
    iso->RequestInterrupt(
        +[](v8::Isolate* iso, void* /*data*/) {
            iso->MemoryPressureNotification(v8::MemoryPressureLevel::kCritical);
            iso->RequestGarbageCollectionForTesting(v8::Isolate::kFullGarbageCollection);
        },
        nullptr);
}

void Script::RefreshLastStackTrace(int32_t maxFrames) {
    auto* iso = isolate_.load().get();
    if (iso == nullptr) {
        return;
    }

    v8::HandleScope scope(iso);
    const v8::Local<v8::StackTrace> trace =
        v8::StackTrace::CurrentStackTrace(iso, maxFrames, v8::StackTrace::kDetailed);
    const int32_t frameCount = trace->GetFrameCount();

    const std::string baseStr = config::GetAppConfig().GetScriptPaths().basePath.string();

    std::vector<StackFrame> frames;
    frames.reserve(static_cast<size_t>(frameCount));
    for (int32_t i = 0; i < frameCount; ++i) {
        const v8::Local<v8::StackFrame> v8frame = trace->GetFrame(iso, static_cast<uint32_t>(i));
        const v8::String::Utf8Value funcName(iso, v8frame->GetFunctionName());
        const v8::String::Utf8Value scriptName(iso, v8frame->GetScriptName());
        StackFrame f;
        if (*funcName != nullptr && funcName.length() > 0) {
            f.functionName.assign(*funcName, static_cast<size_t>(funcName.length()));
        }
        if (*scriptName != nullptr && scriptName.length() > 0) {
            // Trim base path so stack traces show e.g. "libs/common/Town.js" not the full install path.
            const std::string_view raw{*scriptName, static_cast<size_t>(scriptName.length())};
            f.scriptName = TrimScriptBase(raw, baseStr);
        }
        f.line = v8frame->GetLineNumber();
        f.column = v8frame->GetColumn();
        frames.push_back(std::move(f));
    }

    auto snapshot = std::make_shared<StackTraceSnapshot>();
    snapshot->frames = std::move(frames);
    lastStackTrace_.store(std::move(snapshot));
}

// ============================================================================
// Event System
// ============================================================================

void Script::RegisterEvent(const std::string& eventName, v8::Local<v8::Function> func) {
    if (eventName.empty())
        return;
    auto* iso = isolate_.load().get();
    if (!iso)
        return;
    std::scoped_lock lock(eventFunctionsMutex_);
    eventFunctions_[eventName].emplace_back(iso, func);
}

void Script::UnregisterEvent(const std::string& eventName, v8::Local<v8::Function> func) {
    if (eventName.empty())
        return;
    std::scoped_lock lock(eventFunctionsMutex_);
    auto it = eventFunctions_.find(eventName);
    if (it == eventFunctions_.end())
        return;

    std::erase_if(it->second, [&func](const v8::Global<v8::Function>& function) {
        return function.Get(func->GetIsolate()) == func;
    });
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
    eventFunctions_.erase(eventName);
}

void Script::ClearAllEvents() {
    std::scoped_lock lock(eventFunctionsMutex_);
    eventFunctions_.clear();
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
    auto* iso = isolate_.load().get();
    if (!iso)
        return;
    auto* platform = V8Host::GetPlatform();
    auto stopToken = thread_.get_stop_token();

    // When paused, sleep without processing events.
    while (state_.load() == ScriptState::Paused && !stopToken.stop_requested()) {
        speedhack::SpeedhackDisabledScope realWaits;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto deadline = std::chrono::steady_clock::now() + duration;
    // Slice wait: real 1ms wall sleep while the remaining virtual budget
    // is at least 1ms wall (= `speed` ms virtual); yield once we'd overshoot.
    const float speed = speedhack::GetSpeed();
    do {  // NOLINT(cppcoreguidelines-avoid-do-while) - must process events before checking deadline
        while (v8::platform::PumpMessageLoop(platform, iso)) {}

        // Pump any queued Chrome DevTools (inspector) messages on the isolate
        // thread alongside V8's own task queue.
        if (inspector_) {
            inspector_->DrainIncoming();
        }

        // Periodically cache heap stats for cross-thread readers.
        // GetHeapStatistics iterates all heap spaces (not cheap), so throttle.
        UpdateHeapStats();

        const auto remainingVirtMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remainingVirtMs <= 0) {
            break;
        }
        if (static_cast<float>(remainingVirtMs) < speed) {
            std::this_thread::yield();
        } else {
            speedhack::SpeedhackDisabledScope realWaits;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } while (state_.load() == ScriptState::Running && !stopToken.stop_requested() &&
             std::chrono::steady_clock::now() < deadline &&
             (mode_ != ScriptMode::InGame || game::GetGameState() == game::GameState::InGame));
}

bool Script::ExecuteEvent(const std::shared_ptr<BaseEvent>& event) {
    auto* iso = isolate_.load().get();
    if (!iso)
        return false;

    if (thread_.get_id() == std::this_thread::get_id()) {
        // Same thread - execute synchronously
        v8::HandleScope scope(iso);
        std::vector<v8::Local<v8::Function>> fns;
        {
            // Copy handlers out from under the lock to avoid re-entrance deadlocks
            std::scoped_lock lock(eventFunctionsMutex_);
            auto it = eventFunctions_.find(std::string(event->Name()));
            if (it != eventFunctions_.end()) {
                fns.reserve(it->second.size());
                for (auto& globalFn : it->second) {
                    fns.push_back(globalFn.Get(iso));
                }
            }
        }

        // Always call Execute - even with empty fns, BlockableEvent needs to
        // decrement its remaining_ counter to avoid stalling the game thread.
        event->Execute(iso, fns);

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

    // Different thread - post to script's foreground task runner.
    // The task is picked up by PumpMessageLoop during delay() calls.
    return PostEvent(event);
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

    auto* platform = V8Host::GetPlatform();
    auto runner = platform->GetForegroundTaskRunner(iso.get());
    // Ensures BlockableEvent::remaining_ is decremented even if the task is dropped or the isolate tears down
    // mid-dispatch.
    auto guard = std::make_shared<DeferGuard>([event] { event->OnDropped(); });
    runner->PostDelayedTask(std::make_unique<LambdaTask>([weak = weak_from_this(), event, guard] {
                                if (auto self = weak.lock()) {
                                    if (self->ExecuteEvent(event)) {
                                        guard->Dismiss();
                                    }
                                }
                            }),
                            static_cast<double>(delayMs) / 1000.0);
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

    auto* iso = isolate_.load().get();
    if (!iso)
        return false;

    auto context = GetContext();
    v8::TryCatch tryCatch(iso);

    // Read file
    std::ifstream file(absolutePath, std::ios::binary);
    if (!file.is_open()) {
        logger_->warn("Failed to open include: {}", absolutePath.string());
        return false;
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Match RunScript: absolute path so kolbot's require.js stack-trace regex finds the d2bs\ segment.
    v8::Local<v8::Script> script;
    if (!framework::script::CompileSource(iso, context, std::move(source), absolutePath.string()).ToLocal(&script)) {
        logger_->warn("Failed to compile include: {}", absolutePath.string());
        if (tryCatch.HasCaught() && !tryCatch.HasTerminated()) {
            ReportException(tryCatch);
            tryCatch.ReThrow();
        }
        return false;
    }

    // Execute
    inProgressIncludes_.emplace(normalized);

    v8::Local<v8::Value> result;
    if (script->Run(context).ToLocal(&result)) {
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

void Script::AddDrawable(std::shared_ptr<framework::drawing::Drawable> drawable) {
    if (!drawable) {
        return;
    }
    std::unique_lock lock(drawablesMutex_);
    drawables_.push_back(std::move(drawable));
}

void Script::RemoveDrawable(const std::shared_ptr<framework::drawing::Drawable>& drawable, bool fireLeaveEvent) {
    if (!drawable) {
        return;
    }

    // Snapshot any hover-leave handler under the lock, then pre-Reset Globals
    // and run onDestroy on the script thread. The pre-Reset means a
    // game-thread reader holding a shared_ptr via GetDrawables() finds empty
    // Globals when ~Drawable eventually runs, making its defensive Reset a
    // no-op. Running onDestroy here (script thread) keeps the
    // V8InstanceTracker bucket aligned with the Increment thread. Dispatch
    // the leave event after releasing the lock so a JS callback can safely
    // re-enter Add/RemoveDrawable without self-deadlock.
    v8::Global<v8::Function> leaveHandler;
    {
        std::unique_lock lock(drawablesMutex_);
        auto it = std::ranges::find(drawables_, drawable);
        if (it == drawables_.end()) {
            return;
        }
        drawables_.erase(it);

        if (fireLeaveEvent && drawable->isHovered.load() && !drawable->onHover.IsEmpty()) {
            if (auto iso = isolate_.load()) {
                leaveHandler = v8::Global<v8::Function>(iso.get(), drawable->onHover);
            }
        }
        drawable->onClick.Reset();
        drawable->onHover.Reset();
        if (drawable->onDestroy) {
            drawable->onDestroy();
            drawable->onDestroy = nullptr;
        }
    }
    if (!leaveHandler.IsEmpty()) {
        ExecuteEvent(std::make_shared<ScreenHookHoverEvent>(game::Point::Zero, false, std::move(leaveHandler)));
    }
}

std::vector<std::shared_ptr<framework::drawing::Drawable>> Script::GetDrawables() {
    std::shared_lock lock(drawablesMutex_);
    return drawables_;
}

}  // namespace d2bs
