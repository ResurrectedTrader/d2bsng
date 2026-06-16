#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <string>

#include "components/console/Panel.h"
#include "game/Console.h"

namespace d2bs::framework::console {

// Read-only log viewer. Receives Print + Log source messages (EvaluateResult
// is routed to ConsolePanel). Filters: source toggles, min level, per-script
// visibility (auto-populated from incoming names), and a free-form text
// substring search. Render-thread only - Console drains its cross-thread
// queue into here via Append().
class LogPanel : public Panel {
   public:
    LogPanel() = default;

    [[nodiscard]] const char* Title() const override { return "Log"; }
    void Draw() override;

    // Append a message; timestamp captured here so display time reflects
    // emission order, not render time. Records msg.name in the script
    // visibility map so the filter UI sees the script. Render-thread only.
    void Append(d2bs::game::console::Message msg);

   private:
    struct Entry {
        d2bs::game::console::Message msg;
        std::chrono::system_clock::time_point ts;
    };

    [[nodiscard]] bool SourceVisible(d2bs::game::console::MessageSource source) const;
    [[nodiscard]] bool MatchesFilter(const Entry& entry) const;
    void DrawFilterBar();
    void DrawScrollback();
    void CopyViewToClipboard() const;

    std::deque<Entry> scrollback_;

    // One row per source kind LogPanel handles (Print + Log;
    // EvaluateResult/ConsolePrint go to ConsolePanel instead).
    struct SourceFilter {
        d2bs::game::console::MessageSource source;
        bool visible = true;
    };
    std::array<SourceFilter, 2> sourceFilters_{
        SourceFilter{.source = d2bs::game::console::MessageSource::Print, .visible = true},
        SourceFilter{.source = d2bs::game::console::MessageSource::Log, .visible = true},
    };
    d2bs::game::console::MessageLevel minLevel_ = d2bs::game::console::MessageLevel::Trace;

    // Auto-populated as messages arrive. Stable iteration order via std::map.
    // Default value on insert is `true` (visible).
    std::map<std::string, bool> scriptVisibility_;

    std::array<char, 256> textFilter_{};

    static constexpr size_t MAX_SCROLLBACK = 5000;
};

}  // namespace d2bs::framework::console
