#include <windows.h>

#include "components/Host.h"
#include "game/Bridge.h"
#include "hooks/Intercepts.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
    switch (ulReasonForCall) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Install inline patches before any other thread can run game code.
            // The game's WinMain calls FindWindowA very early; this pre-empts the race.
            // Both calls are idempotent so the async init path in js::Host::Initialize
            // is a safe no-op. Bridge::Init pops a MessageBoxW per failure and returns
            // false to abort DLL load on any step failure.
            if (!d2bs::game::Bridge::Init()) {
                return FALSE;
            }
            d2bs::hooks::intercepts::InstallAll();
            d2bs::js::Host::Initialize(hModule);
            break;
        case DLL_PROCESS_DETACH:
            if (lpReserved == nullptr) {
                d2bs::js::Host::Shutdown();
            }
            break;
        default:
            break;
    }

    return TRUE;
}
