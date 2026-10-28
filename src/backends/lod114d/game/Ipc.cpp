// game::SendIPC in a file of its own: a host that answers the manager's messages itself builds the backend without
// this file and defines SendIPC (docs/host_hooks.md).

#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "game/GameHelpers.h"
#include "imports/D2Gfx.h"
#include "utils/Profiling.h"
#include "utils/utils.h"

namespace d2bs::game {

using namespace d2bs::lod114d::imports;

int32_t SendIPC(uint32_t mode, std::string_view data, uintptr_t targetHwnd, std::string_view windowClassName,
                std::string_view windowName) {
    HWND target = reinterpret_cast<HWND>(targetHwnd);
    if (target == nullptr) {
        std::wstring classWide;
        std::wstring nameWide;
        const wchar_t* classPtr = nullptr;
        const wchar_t* namePtr = nullptr;
        if (!windowClassName.empty()) {
            classWide = utils::ToWStr(std::string(windowClassName), CP_UTF8);
            classPtr = classWide.c_str();
        }
        if (!windowName.empty()) {
            nameWide = utils::ToWStr(std::string(windowName), CP_UTF8);
            namePtr = nameWide.c_str();
        }
        // Both empty -> FindWindowW(NULL, NULL) returns the broadcast HWND
        // (per reference behaviour the recipient is the foreground window).
        target = FindWindowW(classPtr, namePtr);
    }
    if (target == nullptr) {
        return 0;
    }
    // string_view::data() need not be NUL-terminated; copy to ensure it is.
    const std::string buffer{data};
    COPYDATASTRUCT cds{};
    cds.dwData = mode;
    cds.cbData = static_cast<DWORD>(buffer.size()) + 1U;  // include null terminator
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - COPYDATASTRUCT.lpData isn't mutated
    cds.lpData = const_cast<char*>(buffer.c_str());
    // Synchronous across processes: this thread is parked until the manager handles the message.
    const utils::profiling::ScopedSleep waiting;
    return static_cast<int32_t>(SendMessageW(target, WM_COPYDATA, reinterpret_cast<WPARAM>(d2gfx::WINDOW_GetWindow()),
                                             reinterpret_cast<LPARAM>(&cds)));
}

}  // namespace d2bs::game
