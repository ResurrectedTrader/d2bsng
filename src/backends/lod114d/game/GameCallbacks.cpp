#include "game/GameCallbacks.h"

#include "hooks/HookManager.h"

namespace d2bs::game {

namespace {
GameCallbacks activeCallbacks;
}  // namespace

void InstallHooks(const GameCallbacks& callbacks) {
    // Copy into static storage - HookManager holds &activeCallbacks for the subsystem's lifetime.
    activeCallbacks = callbacks;
    hooks::Install(&activeCallbacks);
}

void RemoveHooks() {
    hooks::Remove();
    activeCallbacks = {};
}

}  // namespace d2bs::game
