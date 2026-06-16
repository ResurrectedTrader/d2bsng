#pragma once

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "components/script/ScriptTypes.h"
#include "game/Console.h"

namespace d2bs::framework::console::theme {

[[nodiscard]] ImVec4 ColorForCode(d2bs::game::console::ColorCode code);
[[nodiscard]] ImVec4 ColorForLevel(d2bs::game::console::MessageLevel level);
[[nodiscard]] ImVec4 ColorForState(d2bs::ScriptState state);

[[nodiscard]] std::string_view LevelTag(d2bs::game::console::MessageLevel level);
[[nodiscard]] std::string_view SourceTag(d2bs::game::console::MessageSource source);

[[nodiscard]] std::string FormatTimestamp(std::chrono::system_clock::time_point tp);
[[nodiscard]] std::string FormatBytes(uint64_t bytes);

}  // namespace d2bs::framework::console::theme
