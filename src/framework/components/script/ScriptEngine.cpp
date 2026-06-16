#include "ScriptEngine.h"

#include <algorithm>
#include <ranges>

#include "components/config/AppConfig.h"
#include "components/inspector/InspectorServer.h"
#include "components/v8/V8Host.h"
#include "utils/utils.h"

namespace d2bs {

ScriptEngine& ScriptEngine::Instance() {
    static ScriptEngine instance;
    return instance;
}

void ScriptEngine::Initialize() {
    if (initialized_)
        return;

    logger_->info("Initializing ScriptEngine");

    (void)V8Host::GetPlatform();

    // Start the V8 inspector server when enabled (inspectorPort > 0), before any
    // script registers a target. Every script isolate attaches a target
    // regardless; the server just exposes them when running.
    if (const int32_t port = config::GetAppConfig().inspectorPort.load(); port > 0) {
        if (framework::inspector::InspectorServer::Instance().Start(static_cast<uint16_t>(port))) {
            logger_->info("V8 inspector listening on http://127.0.0.1:{} - open chrome://inspect", port);
        } else {
            logger_->error("V8 inspector failed to bind port {}", port);
        }
    }

    // Create the persistent console script
    CreateConsoleScript();

    initialized_ = true;
    logger_->info("ScriptEngine initialized");
}

void ScriptEngine::Shutdown() {
    if (!initialized_)
        return;

    logger_->info("Shutting down ScriptEngine");

    StopAllScripts();

    std::shared_ptr<Script> consoleScript;
    {
        std::unique_lock lock(consoleMutex_);
        consoleScript = std::move(consoleScript_);
    }
    if (consoleScript) {
        consoleScript->Stop();
    }

    // Join outside the lock - scripts may call ScriptEngine methods during cleanup.
    {
        std::vector<std::shared_ptr<Script>> scriptsToJoin;
        {
            std::shared_lock lock(scriptsMutex_);
            for (auto& script : scripts_ | std::views::values) {
                scriptsToJoin.push_back(script);
            }
        }

        for (auto& script : scriptsToJoin) {
            logger_->debug("Joining script: {}", script->GetName());
            script->Join();
        }
    }

    if (consoleScript) {
        logger_->debug("Joining console script");
        consoleScript->Join();
    }

    {
        std::unique_lock lock(scriptsMutex_);
        scripts_.clear();
    }

    framework::inspector::InspectorServer::Instance().Stop();

    // V8 platform shutdown is handled by V8Host singleton destructor
    initialized_ = false;
    logger_->info("ScriptEngine shutdown complete");
}

std::shared_ptr<Script> ScriptEngine::StartScript(const std::filesystem::path& path, ScriptMode mode,
                                                  std::vector<std::vector<uint8_t>> args) {
    auto normalizedTarget = Script::NormalizePath(path);

    // Stop any existing script at the same path, then atomically insert the new one. Matches reference d2bs behavior.
    std::shared_ptr<Script> staleScript;
    auto script = std::make_shared<Script>(path, mode, std::move(args));
    {
        std::unique_lock lock(scriptsMutex_);
        auto it = std::ranges::find_if(
            scripts_, [&](const auto& entry) { return entry.second->GetNormalizedPath() == normalizedTarget; });
        if (it != scripts_.end()) {
            staleScript = it->second;
            staleScript->Stop();
            scripts_.erase(it);
        }
        script->Start();
        scripts_.emplace(script->GetThreadId(), script);
    }

    // Join outside the lock - the dying script's RemoveSelfFromEngine acquires
    // scriptsMutex_ (entry is already erased so it's a no-op, but the lock
    // acquisition still occurs).
    if (staleScript) {
        staleScript->Join();
    }

    return script;
}

void ScriptEngine::RemoveScript(std::thread::id threadId) {
    std::unique_lock lock(scriptsMutex_);
    scripts_.erase(threadId);
}

void ScriptEngine::StopScript(std::thread::id threadId) {
    std::shared_lock lock(scriptsMutex_);

    auto it = scripts_.find(threadId);
    if (it != scripts_.end()) {
        it->second->Stop();
    }
}

void ScriptEngine::StopAllScripts() {
    std::shared_lock lock(scriptsMutex_);

    for (auto& script : scripts_ | std::views::values) {
        // Exclude console script - it should persist across stop-all calls
        if (script->GetMode() != ScriptMode::Console) {
            script->Stop();
        }
    }
}

std::shared_ptr<Script> ScriptEngine::GetScript(std::thread::id threadId) {
    std::shared_lock lock(scriptsMutex_);

    auto it = scripts_.find(threadId);
    if (it != scripts_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Script> ScriptEngine::GetScriptByPath(const std::filesystem::path& path) {
    std::shared_lock lock(scriptsMutex_);

    auto normalizedTarget = Script::NormalizePath(path);
    for (auto& script : scripts_ | std::views::values) {
        if (script->GetNormalizedPath() == normalizedTarget) {
            return script;
        }
        if (Script::NormalizePath(script->GetName()) == normalizedTarget) {
            return script;
        }
        // Reference: FindScriptByName in reference/d2bs/JSScript.cpp compares
        // against GetShortFilename() which strips the script base path prefix.
        if (Script::NormalizePath(script->GetPath().filename()) == normalizedTarget) {
            return script;
        }
    }
    return nullptr;
}

Script* ScriptEngine::GetScript(v8::Isolate* iso) {
    if (!iso) {
        iso = v8::Isolate::TryGetCurrent();
    }
    if (!iso) {
        return nullptr;
    }
    return static_cast<Script*>(iso->GetData(0));
}

std::vector<std::shared_ptr<Script>> ScriptEngine::GetAllScripts() {
    std::shared_lock lock(scriptsMutex_);

    std::vector<std::shared_ptr<Script>> result;
    result.reserve(scripts_.size());

    for (auto& script : scripts_ | std::views::values) {
        result.push_back(script);
    }

    return result;
}

void ScriptEngine::Evaluate(const std::string& code) {
    if (auto script = GetConsoleScript(); script && script->GetState() == ScriptState::Running) {
        script->Evaluate(code);
    }
}

void ScriptEngine::SetInspector(bool enabled, int32_t port) {
    // Ignore toggles before init / after shutdown - a UI-thread enable racing
    // Shutdown()'s Stop() could otherwise leave a listening socket with no scripts.
    if (!initialized_.load()) {
        return;
    }
    port = std::clamp(port, config::MIN_INSPECTOR_PORT, config::MAX_INSPECTOR_PORT);

    // Reconcile the server to the new state. Stop unconditionally first so a port
    // change while enabled rebinds; targets survive a stop (scripts keep their
    // ScriptInspectors), so they reappear as soon as the server is back up.
    auto& server = framework::inspector::InspectorServer::Instance();
    server.Stop();
    bool listening = false;
    if (enabled) {
        listening = server.Start(static_cast<uint16_t>(port));
        if (!listening) {
            logger_->error("V8 inspector failed to bind port {}", port);
        }
    }
    // Persist enabled (positive) only when the server is actually listening; a
    // failed bind (e.g. the port is already taken by another instance - common
    // when multi-boxing on the default port) falls back to disabled so the stored
    // sign and the Settings checkbox reflect reality, while remembering the port.
    config::GetAppConfig().inspectorPort.store(listening ? port : -port);
}

void ScriptEngine::CreateConsoleScript() {
    auto paths = config::GetAppConfig().GetScriptPaths();
    std::filesystem::path consolePath;

    // Look for a console script file in the script base path. Honors
    // scriptPaths.consoleScript (populated from [settings]/DefaultConsoleScript
    // and overridable per-profile), falling back to "console.js" when unset.
    if (!paths.basePath.empty()) {
        const auto& name = paths.consoleScript.empty() ? "console.js" : paths.consoleScript;
        auto candidate = paths.basePath / name;
        if (std::filesystem::exists(candidate)) {
            consolePath = candidate;
        }
    }

    // Create console script - empty path uses built-in event loop main()
    auto newScript = std::make_shared<Script>(consolePath, ScriptMode::Console);
    newScript->Start();

    // Add to scripts_ map so ForEachScript dispatches events to the console
    {
        std::unique_lock lock(scriptsMutex_);
        scripts_.emplace(newScript->GetThreadId(), newScript);
    }
    std::unique_lock lock(consoleMutex_);
    consoleScript_ = std::move(newScript);
}

void ScriptEngine::RestartConsoleScript() {
    // Extract under lock so concurrent readers stop seeing the dying script immediately.
    std::shared_ptr<Script> oldScript;
    {
        std::unique_lock lock(consoleMutex_);
        oldScript = std::move(consoleScript_);
    }
    if (oldScript) {
        logger_->info("Stopping console script for restart");
        // Capture the thread id *before* Join - once the jthread completes,
        // thread_.get_id() returns std::thread::id{} (default / not-a-thread),
        // and erase(default_id) would silently do nothing, leaving the stopped
        // entry in scripts_. That's how stopped consoles used to accumulate.
        const auto tid = oldScript->GetThreadId();
        oldScript->Stop();
        oldScript->Join();
        std::unique_lock lock(scriptsMutex_);
        scripts_.erase(tid);
    }

    logger_->info("Restarting console script");
    CreateConsoleScript();
}

}  // namespace d2bs
