#pragma once

#include <map>
#include <memory>
#include <ranges>
#include <shared_mutex>
#include <vector>
#include "Script.h"
#include "utils/utils.h"

namespace d2bs {

class ScriptEngine {
   public:
    static ScriptEngine& Instance();

    // Lifecycle
    void Initialize();
    void Shutdown();
    bool IsInitialized() const { return initialized_.load(); }

    // Script management
    std::shared_ptr<Script> StartScript(const std::filesystem::path& path, ScriptMode mode,
                                        std::vector<std::vector<uint8_t>> args = {});
    void RemoveScript(std::thread::id threadId);
    void StopScript(std::thread::id threadId);
    void StopAllScripts();

    // Returns shared_ptr to prevent dangling pointers across threads
    std::shared_ptr<Script> GetScript(std::thread::id threadId);
    std::shared_ptr<Script> GetScriptByPath(const std::filesystem::path& path);
    // Script owning `iso`. If `iso` is null, uses v8::Isolate::TryGetCurrent()
    // so V8 callbacks can just write `GetScript()` with no argument.
    // Returns nullptr if there's no current isolate or the isolate doesn't
    // belong to a d2bs script (e.g. raw V8 platform threads). Always safe
    // to call cross-thread - the returned pointer is only dereferenced on
    // the isolate's own thread by convention.
    Script* GetScript(v8::Isolate* iso = nullptr);
    std::vector<std::shared_ptr<Script>> GetAllScripts();

    // Snapshots to avoid deadlock when a callback calls ScriptEngine methods.
    template <typename Fn>
    void ForEachScript(const Fn& fn) {
        std::vector<std::shared_ptr<Script>> snapshot;
        {
            std::shared_lock lock(scriptsMutex_);
            snapshot.reserve(scripts_.size());
            for (auto& script : scripts_ | std::views::values) {
                snapshot.push_back(script);
            }
        }
        for (auto& script : snapshot) {
            fn(script);
        }
    }

    // Returns a shared_ptr snapshot so callers can safely use the console script
    // even if another thread concurrently restarts or shuts it down.
    std::shared_ptr<Script> GetConsoleScript() {
        std::shared_lock lock(consoleMutex_);
        return consoleScript_;
    }
    // Enqueue `code` to run as JS on the console script's isolate. This is
    // the raw eval primitive - for command-string dispatch (built-ins +
    // eval fallback) see framework::script::RunCommand in components/script/Commands.h.
    void Evaluate(const std::string& code);
    void RestartConsoleScript();

    // Enable/disable Chrome DevTools debugging and set the listening port in one
    // call. Persists both into AppConfig::inspectorPort via the sign convention
    // (positive = enabled on that port, non-positive = disabled with the
    // magnitude remembered) and reconciles the server: (re)binds when enabled,
    // stops when disabled. Scripts always have a ScriptInspector attached, so
    // this only controls whether the server exposes them. Safe from the UI thread.
    void SetInspector(bool enabled, int32_t port);

    // Non-copyable
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

   private:
    ScriptEngine() = default;
    ~ScriptEngine() = default;

    void CreateConsoleScript();

    // Ordered map: O(log n) lookup by thread ID; stable iteration order matches reference behavior.
    std::map<std::thread::id, std::shared_ptr<Script>> scripts_;
    // Protected by consoleMutex_. Mutation (CreateConsoleScript / RestartConsoleScript /
    // Shutdown) is effectively single-writer from the game thread; reads can come from
    // any thread (e.g. DDE pump thread calling RunCommand, IPC callbacks).
    // All accesses must either go through the GetConsoleScript() accessor or hold
    // consoleMutex_ directly.
    std::shared_ptr<Script> consoleScript_;
    mutable std::shared_mutex consoleMutex_;
    mutable std::shared_mutex scriptsMutex_;

    std::atomic<bool> initialized_ = false;

    std::shared_ptr<spdlog::logger> logger_ = d2bs::utils::GetLogger("ScriptEngine");
};

}  // namespace d2bs
