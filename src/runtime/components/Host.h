#pragma once

#include <spdlog/logger.h>
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <thread>

#include "game/GameCallbacks.h"

namespace d2bs::runtime {

// Orchestrates DLL startup and shutdown.
// Called from DllMain - keeps loader-lock-sensitive code minimal.
class Host {
   public:
    static void Initialize(HMODULE hModule);
    static void Shutdown();
    // True once Initialize finished bringing the frontend up (the script engine, services). Only used by the simulator;
    // d2bs.dll never calls it.
    static bool IsInitialized() { return isInitialized_.load(); }

    Host() = delete;

   private:
    static void DoInitialize(HMODULE hModule);
    static void SetupPaths(HMODULE hModule);
    static void SetupLogging();
    static void LoadConfig();
    static game::GameCallbacks BuildCallbacks();

    // The d2bs.ini to load.
    static std::filesystem::path ConfigPath();

    inline static std::atomic<bool> isInitialized_{false};
    inline static std::jthread initThread_;
    inline static std::filesystem::path dllDir_;
    inline static std::shared_ptr<spdlog::logger> logger_;
    inline static LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter_ = nullptr;
    inline static PVOID vectoredExceptionHandle_ = nullptr;
};

}  // namespace d2bs::runtime
