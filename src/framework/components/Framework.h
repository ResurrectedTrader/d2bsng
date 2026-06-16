#pragma once

#include <spdlog/logger.h>
#include <windows.h>

#include <thread>

#include "game/GameCallbacks.h"

namespace d2bs {

// Orchestrates DLL startup and shutdown.
// Called from DllMain - keeps loader-lock-sensitive code minimal.
class Framework {
   public:
    static void Initialize(HMODULE hModule);
    static void Shutdown();

    Framework() = delete;

   private:
    static void DoInitialize(HMODULE hModule);
    static void SetupPaths(HMODULE hModule);
    static void SetupLogging();
    static void LoadConfig();
    static game::GameCallbacks BuildCallbacks();

    inline static std::jthread initThread_;
    inline static std::shared_ptr<spdlog::logger> logger_;
    inline static LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter_ = nullptr;
    inline static PVOID vectoredExceptionHandle_ = nullptr;
};

}  // namespace d2bs
