#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <string>

#include "components/console/Panel.h"
#include "components/console/RowSelection.h"
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
    void Append(game::console::Message msg);

   private:
    struct Entry {
        game::console::Message msg;
        std::chrono::system_clock::time_point ts;
        uint64_t seq = 0;  // stable row id for selection (see RowSelection)
    };

    [[nodiscard]] bool SourceVisible(game::console::MessageSource source) const;
    [[nodiscard]] bool MatchesFilter(const Entry& entry) const;
    [[nodiscard]] std::string FormatEntry(const Entry& entry) const;
    void DrawFilterBar();
    void DrawScrollback();
    void CopyViewToClipboard() const;
    void CopySelectionToClipboard() const;

    std::deque<Entry> scrollback_;
    RowSelection selection_;
    uint64_t nextSeq_ = 1;  // 0 is reserved as "no row"

    // One row per source kind LogPanel handles (Print + Log;
    // EvaluateResult/ConsolePrint go to ConsolePanel instead).
    struct SourceFilter {
        game::console::MessageSource source;
        bool visible = true;
    };
    std::array<SourceFilter, 2> sourceFilters_{
        SourceFilter{.source = game::console::MessageSource::Print, .visible = true},
        SourceFilter{.source = game::console::MessageSource::Log, .visible = true},
    };
    game::console::MessageLevel minLevel_ = game::console::MessageLevel::Trace;

    // Auto-populated as messages arrive. Stable iteration order via std::map.
    // Default value on insert is `true` (visible).
    std::map<std::string, bool> scriptVisibility_;

    std::array<char, 256> textFilter_{};

    static constexpr size_t MAX_SCROLLBACK = 5000;
};

}  // namespace d2bs::framework::console
