#include "game/Bridge.h"

#include "asm_thunks/asm_thunks.h"
#include "hooks/Intercepts.h"
#include "imports/ImportTypes.h"

// Each include instantiates its inline GameFunc/GameVar/GameAsmFunc objects in this TU; their constructors
// self-register with imports::Registry. Removing any include silently drops all imports in that namespace.
#include "imports/BnClient.h"
#include "imports/D2Client.h"
#include "imports/D2Cmp.h"
#include "imports/D2Common.h"
#include "imports/D2Game.h"
#include "imports/D2Gfx.h"
#include "imports/D2Lang.h"
#include "imports/D2Launch.h"
#include "imports/D2Multi.h"
#include "imports/D2Net.h"
#include "imports/D2Win.h"
#include "imports/Storm.h"

#include <Windows.h>
#include <atomic>

namespace d2bs::game {

namespace {

std::atomic<bool> isInitialized{false};
std::atomic<bool> initSucceeded{false};

}  // namespace

bool Bridge::Init() {
    bool expected = false;
    if (!isInitialized.compare_exchange_strong(expected, true)) {
        // Already attempted (idempotent). Report whatever the first call decided.
        return initSucceeded.load(std::memory_order_acquire);
    }

    auto base = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
    if (base == 0) {
        MessageBoxW(nullptr,
                    L"d2bsng: failed to obtain the Diablo II module base address (GetModuleHandle returned NULL).",
                    L"d2bsng init failure", MB_OK | MB_ICONERROR);
        return false;
    }

    imports::Registry::Get().ResolveAll(base);

    // Seed the naked-asm thunk jump targets. Hook lifecycle is owned by GameCallbacks::InstallHooks.
    if (!asm_thunks::Init()) {
        // asm_thunks::Init already showed its own MessageBox naming the
        // specific unresolved entry; nothing extra to report here.
        return false;
    }
    if (!hooks::intercepts::Init()) {
        // hooks::intercepts::Init already showed its own MessageBox.
        return false;
    }

    initSucceeded.store(true, std::memory_order_release);
    return true;
}

void Bridge::Shutdown() {
    isInitialized.store(false);
    initSucceeded.store(false);
}

}  // namespace d2bs::game
