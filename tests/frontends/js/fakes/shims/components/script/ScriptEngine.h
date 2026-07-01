#pragma once

// Test shim for src/frontends/js/components/script/ScriptEngine.h
//
// Test-only shim for components/script/ScriptEngine.h. Drops the v8-heavy
// isolate/console surface. Only StartScript and ForEachScript - the two hooks
// GameLoop::DriveScriptLifecycle touches - are exposed.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "components/script/Script.h"
#include "components/script/ScriptTypes.h"

namespace d2bs {

class ScriptEngine {
   public:
    static ScriptEngine& Instance();

    std::shared_ptr<Script> StartScript(const std::filesystem::path& path, ScriptMode mode);
    void RestartConsoleScript();
    void StopAllScripts();
    void Evaluate(const std::string& code);
    bool IsInitialized() const { return initialized_; }

    template <typename Fn>
    void ForEachScript(const Fn& fn) {
        auto snapshot = scripts_;
        for (auto& script : snapshot) {
            fn(script);
        }
    }

    // Test helpers (not part of the real ScriptEngine API).
    void Reset();
    void SetInitialized(bool v) { initialized_ = v; }
    void ResetRestartCount() {
        restartConsoleCount_ = 0;
        restartedConsoleName_.clear();
    }
    int32_t RestartConsoleCount() const { return restartConsoleCount_; }
    // consoleScript observed in AppConfig at the moment RestartConsoleScript
    // was last called - used to verify SetScriptPaths ran first.
    const std::string& RestartedConsoleName() const { return restartedConsoleName_; }
    const std::vector<std::shared_ptr<Script>>& StartedScripts() const { return started_; }

   private:
    ScriptEngine() = default;
    std::vector<std::shared_ptr<Script>> scripts_;
    std::vector<std::shared_ptr<Script>> started_;
    int32_t restartConsoleCount_ = 0;
    std::string restartedConsoleName_;
    bool initialized_ = false;
};

}  // namespace d2bs
