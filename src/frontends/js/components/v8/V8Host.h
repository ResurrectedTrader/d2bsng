#pragma once
#include <libloaderapi.h>
#include <libplatform/libplatform.h>
#include <format>
#include <functional>
#include <memory>
#include <thread>

#include <spdlog/spdlog.h>
#include <v8.h>
#include "config/AppConfig.h"
#include "utils/threadutils.h"

// V8Host is a function-local static singleton that owns the V8 platform and engine lifecycle.
// Its destructor calls v8::V8::Dispose() and v8::V8::DisposePlatform().
//
// DESTRUCTION ORDER DEPENDENCY: Multiple other function-local statics hold v8::Global handles
// whose destructors call into V8 (notably the TemplateCache inside each V8ClassBase<T>
// instantiation). If V8Host is destroyed before those caches, the v8::Global destructors
// will call v8::V8::DisposeGlobal() on a disposed engine, causing a crash.
//
// This is mitigated by the explicit shutdown sequence: ScriptEngine::Shutdown() joins all
// script threads, each of which calls Script::TeardownIsolate() -> ClearAllClassCaches(),
// draining every TemplateCache before static destruction begins. Callers MUST ensure
// ScriptEngine::Shutdown() completes before process/DLL teardown reaches static destructors
// (i.e., before atexit / DLL_PROCESS_DETACH finalization).
class V8Host {
   public:
    // Explicitly expose the platform if other isolates need it
    static v8::Platform *GetPlatform() {
        static V8Host instance;
        return instance.platform_.get();
    }

    V8Host(const V8Host &) = delete;
    V8Host &operator=(const V8Host &) = delete;

   private:
    V8Host() {
        v8::SandboxHardwareSupport::InitializeBeforeThreadCreation();
        v8::V8::InitializeICU();

        // One SetFlagsFromString call: user flags (INI [settings]/V8Flags) first,
        // then our mandatory flags so a conflicting user flag can't drop them (V8
        // applies repeated flags in order, last wins). Must run before
        // v8::V8::Initialize(); the config is already loaded by this point.
        const auto &cfg = d2bs::config::GetAppConfig();
        std::string v8Flags = cfg.v8Flags;
        if (!v8Flags.empty()) {
            spdlog::info("Applying user V8 flags: {}", v8Flags);
            v8Flags += ' ';
        }
        v8Flags += "--expose-gc";
        // The single-threaded platform has no worker pool, so V8 requires
        // --single-threaded to stop it posting background tasks that never run.
        if (cfg.v8SingleThreadedPlatform) {
            v8Flags += " --single-threaded";
        }
        v8::V8::SetFlagsFromString(v8Flags.c_str());

        if (cfg.v8SingleThreadedPlatform) {
            platform_ = v8::platform::NewSingleThreadedDefaultPlatform(v8::platform::IdleTaskSupport::kDisabled,
                                                                       v8::platform::InProcessStackDumping::kEnabled);
        } else {
            platform_ = v8::platform::NewDefaultPlatform(cfg.v8ThreadPoolSize, v8::platform::IdleTaskSupport::kDisabled,
                                                         v8::platform::InProcessStackDumping::kEnabled);
        }

        v8::V8::InitializePlatform(platform_.get());

        // 2. Initialize V8 Lib
        v8::V8::Initialize();

        v8::V8::SetDcheckErrorHandler([](const char *file, int line, const char *message) {
            auto dump = std::format("V8 DCHECK failure at {}:{}: {}\n{}\n", file ? file : "<null>", line,
                                    message ? message : "<null>", d2bs::thread_utils::GetThreadStacktrace());
            d2bs::thread_utils::CrashAndExit(dump, 0xD2B50004);
        });
        // V8::SetFatalErrorHandler is the *process-wide* CHECK / fatal-error
        // hook (distinct from Isolate::SetFatalErrorHandler, which is for API
        // usage failures only). Without this, V8 CHECK failures call
        // OS::Abort -> V8_IMMEDIATE_CRASH -> __fastfail, which bypasses every
        // SEH / VEH / UEF we've installed. With this hook, we get the dump
        // and a graceful exit.
        v8::V8::SetFatalErrorHandler([](const char *file, int line, const char *message) {
            auto dump = std::format("V8 fatal/CHECK failure at {}:{}: {}\n{}\n", file ? file : "<null>", line,
                                    message ? message : "<null>", d2bs::thread_utils::GetThreadStacktrace());
            d2bs::thread_utils::CrashAndExit(dump, 0xD2B50005);
        });
        v8::V8::SetFatalMemoryErrorCallback([](const char *location, const v8::OOMDetails &details) {
            auto dump = std::format("V8 OOM at '{}': {} (is_heap_oom={})\n{}\n", location ? location : "<null>",
                                    details.detail ? details.detail : "<null>", details.is_heap_oom,
                                    d2bs::thread_utils::GetThreadStacktrace());
            d2bs::thread_utils::CrashAndExit(dump, 0xD2B50006);
        });
        // ReSharper disable once CppParameterMayBeConstPtrOrRef
        v8::V8::SetUnhandledExceptionCallback([](_EXCEPTION_POINTERS *exceptionPointers) -> int {
            auto stackTrace = d2bs::thread_utils::GetStacktraceFromContext(exceptionPointers->ContextRecord);
            auto description = d2bs::thread_utils::GetThreadDescription();

            auto message = std::format(
                "\n{}\n"
                "*** Unhandled Exception in v8!\n"
                "*** Base address: {:#x}\n"
                "*** Thread id: {}\n"
                "{}"
                "*** ExpCode: {:#x}\n"
                "*** ExpFlags: {:#x}\n"
                "*** ExpAddress: {}\n",
                stackTrace, reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr)), std::this_thread::get_id(),
                description.empty() ? "" : std::format("*** Thread description: {}\n", description),
                exceptionPointers->ExceptionRecord->ExceptionCode, exceptionPointers->ExceptionRecord->ExceptionFlags,
                exceptionPointers->ExceptionRecord->ExceptionAddress);

            d2bs::thread_utils::CrashAndExit(message, exceptionPointers->ExceptionRecord->ExceptionCode);
        });
    }

    ~V8Host() {
        // Intentionally do NOT call V8::Dispose / V8::DisposePlatform here.
        //
        // This destructor runs from the CRT atexit chain on DLL_PROCESS_DETACH.
        // If ScriptEngine::Shutdown hasn't completed (e.g., Game.exe called
        // ExitProcess directly from an __except, or any other ungraceful exit
        // path), live isolates are still attached to the IsolateGroup. V8's
        // IsolateGroup::TearDownOncePerProcess CHECKs `refcount == 1` and
        // V8_Fatal's when it isn't - which masks the actual cause of the
        // exit with a teardown CHECK in our crash dump.
        //
        // The OS reclaims V8's memory, threads, and handles when the process
        // exits regardless, so the only thing we lose by not calling Dispose
        // is the CHECK-induced crash itself.
        platform_.reset();
    }

    std::unique_ptr<v8::Platform> platform_;
};

class LambdaTask : public v8::Task {
   public:
    template <class F>
    explicit LambdaTask(F &&f) : f_(std::forward<F>(f)) {}
    void Run() override { f_(); }

   private:
    std::function<void()> f_;
};
