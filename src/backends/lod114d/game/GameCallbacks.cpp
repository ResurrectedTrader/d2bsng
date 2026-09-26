#include "game/GameCallbacks.h"

#include "hooks/HookManager.h"

namespace d2bs::game {

namespace {
GameCallbacks activeCallbacks;
}  // namespace

bool InstallHooks(const GameCallbacks& callbacks) {
    // Copy into static storage - HookManager holds &activeCallbacks for the subsystem's lifetime.
    activeCallbacks = callbacks;
    lod114d::hooks::Install(&activeCallbacks);
    return true;
}

void RemoveHooks() {
    lod114d::hooks::Remove();
    activeCallbacks = {};
}

}  // namespace d2bs::game
