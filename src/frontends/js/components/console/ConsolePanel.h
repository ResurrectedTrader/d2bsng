#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <imgui.h>

#include "components/console/Panel.h"
#include "components/console/RowSelection.h"
#include "game/Console.h"

namespace d2bs::js::console {

// REPL panel. Receives only EvaluateResult-source messages (Console
// routes other sources to LogPanel). The user types into the input field;
// on Enter the panel both appends a "> input" entry and forwards the line
// to js::script::RunCommand for evaluation in the console isolate.
//
// History navigation (bash-style):
//   Up   / Down       - walk every entry, newest first
//   PgUp / PgDown     - walk only entries that start with the text before
//                       the cursor (prefix-search)
//
// Submitting a command that already exists in history moves it to the
// front rather than creating a duplicate.
class ConsolePanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Console"; }
    void Draw() override;

    // Append an EvaluateResult-source message. Render-thread only.
    void Append(const game::console::Message& msg);

    // Disambiguates input lines (no level - typed by the user) from log
    // lines (which carry a MessageLevel). Without this we couldn't render
    // a "> " prompt for the user's own input.
    enum class Kind { Input, Output, Error };

   private:
    struct Entry {
        Kind kind;
        // Pre-split at submit/append time so DrawColoredText doesn't run
        // text.find('\n') every render frame. Input is always a single
        // line (single-line ImGui::InputText); Output/Error may span many.
        std::vector<std::string> lines;
        uint64_t seq = 0;  // stable per-entry id; folded with line index for rows
    };

    // Stable per-row id: the entry's seq in the high bits, the line index in the
    // low LINE_INDEX_BITS (entries never carry anywhere near 2^LINE_INDEX_BITS
    // lines). Keyed into RowSelection so selection survives entries arriving and
    // being evicted.
    static constexpr uint64_t LINE_INDEX_BITS = 20;
    [[nodiscard]] static uint64_t RowId(uint64_t seq, size_t line) {
        return (seq << LINE_INDEX_BITS) | (static_cast<uint64_t>(line) & ((1ULL << LINE_INDEX_BITS) - 1ULL));
    }
    [[nodiscard]] std::string FormatRow(const Entry& entry, size_t line) const;

    void DrawTranscript();
    void DrawInputLine();
    void SubmitInput();
    void CopyTranscriptToClipboard() const;
    void CopySelectionToClipboard() const;
    void PushHistory(const std::string& cmd);
    void NavigateHistory(int direction, ImGuiInputTextCallbackData* data);
    void PrefixSearchHistory(int direction, ImGuiInputTextCallbackData* data);

    static int InputCallback(ImGuiInputTextCallbackData* data);

    std::deque<Entry> transcript_;
    RowSelection selection_;
    uint64_t nextSeq_ = 1;  // 0 is reserved as "no row"
    std::array<char, 1024> inputBuf_{};
    bool refocusInput_ = true;

    // History walk state. historyCursor_ < 0 means we're looking at the
    // user's in-progress draft; >= 0 indexes into history_ (0 = most recent).
    std::deque<std::string> history_;
    int32_t historyCursor_ = -1;
    std::string draft_;

    static constexpr size_t MAX_ENTRIES = 2000;
    static constexpr size_t MAX_HISTORY = 200;
};

}  // namespace d2bs::js::console
