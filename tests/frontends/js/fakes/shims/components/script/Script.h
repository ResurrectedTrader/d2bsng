#pragma once

// Test shim for src/frontends/js/components/script/Script.h
//
// Test-only shim for components/script/Script.h. The real header depends on
// <v8.h> for isolate/event machinery; GameLoop.cpp only needs Script::GetMode()
// and Script::Stop() via the ForEachScript callback in DriveScriptLifecycle.

#include <filesystem>

#include "components/script/ScriptTypes.h"

namespace d2bs {

class Script {
   public:
    Script(std::filesystem::path path, ScriptMode mode) : path_(std::move(path)), mode_(mode) {}

    ScriptMode GetMode() const { return mode_; }
    const std::filesystem::path& GetPath() const { return path_; }
    void Stop() { stopped_ = true; }
    bool IsStopped() const { return stopped_; }

   private:
    std::filesystem::path path_;
    ScriptMode mode_;
    bool stopped_ = false;
};

}  // namespace d2bs
