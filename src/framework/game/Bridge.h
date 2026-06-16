#pragma once

#include "game/GameLock.h"

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

    // Acquire a shared (read) lock on game memory. Use in V8 callbacks that
    // access multiple game properties for a consistent view within a single frame.
    // Inner ResolvePtr() calls are recursive re-entries (free - just counter increment).
    // RAII - released on scope exit.
    static GameReadLock Lock() { return {}; }

    Bridge() = delete;
};

}  // namespace d2bs::game
