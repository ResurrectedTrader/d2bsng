#pragma once

namespace d2bs::game {

// Static initializer that resolves all fn:: and var:: pointers from Game.exe offsets.
// Call Init() once at DLL_PROCESS_ATTACH, Shutdown() at DLL_PROCESS_DETACH.
//
// Init() returns false if any step (module-base lookup, import resolution,
// asm-thunk snapshotting, intercept snapshotting) fails. On failure the d2bs
// port pops a per-failure-point MessageBoxW describing what went wrong; the
// caller in DllMain must propagate the false return by failing DLL_PROCESS_ATTACH.
class Bridge {
   public:
    [[nodiscard]] static bool Init();
    static void Shutdown();

    Bridge() = delete;
};

}  // namespace d2bs::game
