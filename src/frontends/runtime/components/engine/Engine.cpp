#include "components/engine/Engine.h"

#include <format>
#include <string>

#include <spdlog/spdlog.h>

#include "config/AppConfig.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::runtime::engine {

namespace {

spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("engine");
    return *LOGGER;
}

// Every engine fault, from any thread, including the engine's own.
//
// Fatal: the engine has said it cannot continue and unibind ends the process when this returns, so
// the crash log is written and the process ended here, with d2bs's own exit code and stack.
//
// OutOfMemory is not the same on both engines: V8 treats its heap giving out as fatal and ends the
// process once this returns, while SpiderMonkey reports it into whatever was running and carries on
// - which for a script with a heap limit is the script failing, not the game. So the crash log is
// written and nothing more: exiting here would turn a recoverable script failure into a dead client
// on the engine that can recover, and on the one that cannot, the engine ends the process itself.
void OnEngineFault(const ub::EngineFaultReport& report, ub::CallbackData /*data*/) {
    const bool isFatal = report.fault == ub::EngineFault::Fatal;
    auto dump =
        std::format("{} engine {}{}: {}\n{}\n", ub::Platform::BackendName(), isFatal ? "fatal error" : "out of memory",
                    report.location.empty() ? "" : std::format(" at {}", report.location), report.message,
                    thread_utils::GetThreadStacktrace());
    if (isFatal) {
        thread_utils::CrashAndExit(dump, 0xD2B50005);
    }
    thread_utils::WriteCrashLog(dump);
}

}  // namespace

}  // namespace d2bs::runtime::engine

// Leaked on purpose. Disposing an engine at DLL_PROCESS_DETACH, from the CRT atexit chain, runs
// while isolates may still be attached - Game.exe ending the process from an __except, or any other
// exit that skips ScriptEngine::Shutdown - and V8 then fails a teardown CHECK that masks the real
// cause of the exit in the crash dump. The OS reclaims the engine's memory and threads at process
// exit regardless, so the only thing not disposing loses is that misleading crash.
ub::Platform& Engine::GetPlatform() {
    using d2bs::runtime::engine::Log;
    using d2bs::runtime::engine::OnEngineFault;
    static ub::Platform* platform = [] {
        using d2bs::config::GetAppConfig;
        const auto& cfg = GetAppConfig();
        std::string flags = cfg.engineFlags;
        if (!flags.empty()) {
            Log().info("Applying user engine flags: {}", flags);
        }
        ub::PlatformOptions options{.engineFlags = flags, .onEngineFault = &OnEngineFault};
        if (cfg.engineSingleThreaded) {
            options.workerThreads = 0;
        } else if (cfg.engineThreadPoolSize > 0) {
            options.workerThreads = static_cast<uint32_t>(cfg.engineThreadPoolSize);
        }
        auto* made = new ub::Platform(options);
        // Empty is not zero: the engine keeps its default pool and does not say how big it is.
        const auto workers = ub::Platform::WorkerThreads();
        Log().info("{} {} up with {} worker thread(s)", ub::Platform::BackendName(), ub::Platform::BackendVersion(),
                   workers ? std::to_string(*workers) : std::string("the engine's default"));
        return made;
    }();
    return *platform;
}
