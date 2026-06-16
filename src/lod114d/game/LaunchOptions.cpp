#include "game/LaunchOptions.h"

#include <Windows.h>
#include <shellapi.h>

#include <mutex>
#include <string_view>
#include <utility>

#include "utils/utils.h"

namespace d2bs::game {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables) - process-wide cache
LaunchOptions options;
std::once_flag parsed;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void Parse(LaunchOptions& out) {
    const auto* cmdLine = ::GetCommandLineW();
    if (cmdLine == nullptr) {
        return;
    }

    int32_t argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(cmdLine, &argc);
    if (argv == nullptr) {
        return;
    }

    // argv[0] is the program name; skip it.
    for (int32_t i = 1; i < argc; ++i) {
        const std::wstring_view token{argv[i]};
        const auto nextValue = [&]() -> std::wstring_view {
            if (i + 1 < argc) {
                ++i;
                return std::wstring_view{argv[i]};
            }
            return {};
        };

        if (token == L"-profile") {
            const auto value = nextValue();
            if (!value.empty()) {
                out.profile = d2bs::utils::ToStr(std::wstring{value}, CP_UTF8);
            }
        } else if (token == L"-multi") {
            out.multiInstance = true;
        } else if (token == L"-title") {
            const auto value = nextValue();
            if (!value.empty()) {
                out.windowTitle = std::wstring{value};
            }
        } else if (token == L"-d2c") {
            const auto value = nextValue();
            if (!value.empty()) {
                out.classicCdKey = d2bs::utils::ToStr(std::wstring{value}, CP_ACP);
            }
        } else if (token == L"-d2x") {
            const auto value = nextValue();
            if (!value.empty()) {
                out.lodCdKey = d2bs::utils::ToStr(std::wstring{value}, CP_ACP);
            }
        } else if (token == L"-ftj") {
            out.reduceFailToJoin = true;
        } else if (token == L"-cachefix") {
            out.randomizeBnetCache = true;
        } else if (token == L"-proxy") {
            const auto value = nextValue();
            if (!value.empty()) {
                out.proxy = d2bs::utils::ToStr(std::wstring{value}, CP_UTF8);
            }
        }
    }

    ::LocalFree(static_cast<HLOCAL>(static_cast<void*>(argv)));
}

}  // namespace

const LaunchOptions& GetLaunchOptions() {
    std::call_once(parsed, [] { Parse(options); });
    return options;
}

}  // namespace d2bs::game
