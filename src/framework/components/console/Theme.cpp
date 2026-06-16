#include "components/console/Theme.h"

#include <fmt/format.h>

#include <array>
#include <ctime>
#include <string_view>

namespace d2bs::framework::console::theme {

using d2bs::game::console::ColorCode;
using d2bs::game::console::MessageLevel;
using d2bs::game::console::MessageSource;

ImVec4 ColorForCode(ColorCode code) {
    switch (code) {
        case ColorCode::White:
            return {1.00F, 1.00F, 1.00F, 1.00F};
        case ColorCode::Red:
            return {1.00F, 0.35F, 0.35F, 1.00F};
        case ColorCode::NeonGreen:
            return {0.30F, 1.00F, 0.30F, 1.00F};
        case ColorCode::Blue:
            return {0.45F, 0.55F, 1.00F, 1.00F};
        case ColorCode::DarkGold:
            return {0.78F, 0.60F, 0.30F, 1.00F};
        case ColorCode::Grey:
            return {0.60F, 0.60F, 0.60F, 1.00F};
        case ColorCode::Black:
            return {0.45F, 0.45F, 0.45F, 1.00F};
        case ColorCode::LightGold:
            return {0.95F, 0.80F, 0.45F, 1.00F};
        case ColorCode::Orange:
            return {1.00F, 0.55F, 0.15F, 1.00F};
        case ColorCode::Yellow:
            return {1.00F, 1.00F, 0.35F, 1.00F};
        case ColorCode::DarkGreen:
            return {0.35F, 0.70F, 0.35F, 1.00F};
        case ColorCode::Purple:
            return {0.80F, 0.40F, 0.95F, 1.00F};
        case ColorCode::Green:
            return {0.35F, 0.85F, 0.35F, 1.00F};
    }
    return {1.00F, 1.00F, 1.00F, 1.00F};
}

ImVec4 ColorForLevel(MessageLevel level) {
    switch (level) {
        case MessageLevel::Trace:
            return {0.55F, 0.55F, 0.55F, 1.00F};
        case MessageLevel::Debug:
            return {0.65F, 0.65F, 0.85F, 1.00F};
        case MessageLevel::Info:
            return {0.85F, 0.85F, 0.85F, 1.00F};
        case MessageLevel::Warn:
            return {1.00F, 0.75F, 0.30F, 1.00F};
        case MessageLevel::Error:
            return {1.00F, 0.45F, 0.45F, 1.00F};
        case MessageLevel::Critical:
            return {1.00F, 0.15F, 0.15F, 1.00F};
    }
    return {0.85F, 0.85F, 0.85F, 1.00F};
}

ImVec4 ColorForState(d2bs::ScriptState state) {
    switch (state) {
        case d2bs::ScriptState::Starting:
            return {1.00F, 0.85F, 0.30F, 1.00F};
        case d2bs::ScriptState::Ready:
            return {0.55F, 0.85F, 1.00F, 1.00F};
        case d2bs::ScriptState::Running:
            return {0.35F, 0.85F, 0.35F, 1.00F};
        case d2bs::ScriptState::Paused:
            return {1.00F, 1.00F, 0.30F, 1.00F};
        case d2bs::ScriptState::Stopping:
            return {1.00F, 0.55F, 0.15F, 1.00F};
        case d2bs::ScriptState::Stopped:
            return {0.60F, 0.60F, 0.60F, 1.00F};
    }
    return {0.85F, 0.85F, 0.85F, 1.00F};
}

std::string_view LevelTag(MessageLevel level) {
    switch (level) {
        case MessageLevel::Trace:
            return "trace";
        case MessageLevel::Debug:
            return "debug";
        case MessageLevel::Info:
            return "info";
        case MessageLevel::Warn:
            return "warn";
        case MessageLevel::Error:
            return "error";
        case MessageLevel::Critical:
            return "crit";
    }
    return "?";
}

std::string_view SourceTag(MessageSource source) {
    switch (source) {
        case MessageSource::Print:
            return "print";
        case MessageSource::ConsolePrint:
            return "console";
        case MessageSource::EvaluateResult:
            return "eval";
        case MessageSource::Log:
            return "log";
    }
    return "?";
}

std::string FormatTimestamp(std::chrono::system_clock::time_point tp) {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    const auto t = std::chrono::system_clock::to_time_t(tp);
    const auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    return fmt::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int32_t>(ms.count()));
}

std::string FormatBytes(uint64_t bytes) {
    using namespace std::string_view_literals;
    constexpr std::array UNITS = {"B"sv, "KiB"sv, "MiB"sv, "GiB"sv, "TiB"sv};
    double v = static_cast<double>(bytes);
    size_t unit = 0;
    while (v >= 1024.0 && unit + 1 < UNITS.size()) {
        v /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return fmt::format("{} {}", bytes, UNITS[0]);
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by UNITS.size() in loop above
    return fmt::format("{:.2f} {}", v, UNITS[unit]);
}

}  // namespace d2bs::framework::console::theme
