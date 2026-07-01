#include "components/console/ConsolePanel.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <string_view>
#include <vector>

#include "components/console/Theme.h"
#include "components/script/Commands.h"
#include "game/Console.h"

namespace d2bs::js::console {

namespace {

using game::console::Message;
using game::console::MessageLevel;
using game::console::SplitByColor;
using game::console::StripColor;

constexpr ImVec4 PROMPT_COLOR{0.55F, 0.55F, 0.55F, 1.00F};
constexpr ImVec4 INPUT_COLOR{0.55F, 0.75F, 1.00F, 1.00F};
constexpr ImVec4 ERROR_COLOR{1.00F, 0.45F, 0.45F, 1.00F};

// Split `text` on '\n' into individual lines. Empty trailing line is preserved
// when text ends in a newline so the rendered transcript keeps the trailing
// blank row (matches the original render behaviour).
[[nodiscard]] std::vector<std::string> SplitLines(std::string_view text) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t nl = text.find('\n', pos);
        const size_t lineEnd = (nl == std::string_view::npos) ? text.size() : nl;
        out.emplace_back(text.substr(pos, lineEnd - pos));
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
    return out;
}

void DrawColoredLine(const std::string& line) {
    // One source line per ImGui row. Within the line, split by colour codes and
    // stitch segments with SameLine(0,0). Embedded '\n' is impossible here - the
    // caller has already split entries into single lines.
    bool firstSeg = true;
    for (const auto& seg : SplitByColor(line)) {
        if (seg.text.empty()) {
            continue;
        }
        if (!firstSeg) {
            ImGui::SameLine(0.0F, 0.0F);
        }
        firstSeg = false;
        ImGui::TextColored(theme::ColorForCode(seg.colorCode), "%s", seg.text.c_str());
    }
    if (firstSeg) {
        // Blank line - emit a NewLine so vertical spacing is preserved.
        ImGui::NewLine();
    }
}

}  // namespace

void ConsolePanel::Append(const Message& msg) {
    const Kind kind =
        (msg.level == MessageLevel::Error || msg.level == MessageLevel::Critical) ? Kind::Error : Kind::Output;
    transcript_.push_back(Entry{
        .kind = kind,
        .lines = SplitLines(msg.text),
        .seq = nextSeq_++,
    });
    while (transcript_.size() > MAX_ENTRIES) {
        transcript_.pop_front();
    }
}

void ConsolePanel::SubmitInput() {
    std::string raw{inputBuf_.data()};
    std::string_view input{raw};
    // Strip leading '/' so the user can type either `stop` or `/stop`. The
    // latter is the conventional slash-command form and would otherwise be
    // parsed by the JS-eval fallback as a regex literal.
    if (!input.empty() && input.front() == '/') {
        input.remove_prefix(1);
    }
    if (input.empty()) {
        return;
    }

    transcript_.push_back(Entry{
        .kind = Kind::Input,
        // Input is single-line (ImGui::InputText, not InputTextMultiline)
        // so we know there's exactly one line - no need to scan for '\n'.
        .lines = {std::string{input}},
        .seq = nextSeq_++,
    });
    while (transcript_.size() > MAX_ENTRIES) {
        transcript_.pop_front();
    }

    PushHistory(raw);
    historyCursor_ = -1;
    draft_.clear();

    script::RunCommand(std::string{input});

    inputBuf_.fill('\0');
    refocusInput_ = true;
}

void ConsolePanel::PushHistory(const std::string& cmd) {
    // Move-to-front semantics: any prior identical entry is removed first
    // so the same command never appears twice in history_.
    if (auto it = std::ranges::find(history_, cmd); it != history_.end()) {
        history_.erase(it);
    }
    history_.push_front(cmd);
    while (history_.size() > MAX_HISTORY) {
        history_.pop_back();
    }
}

void ConsolePanel::NavigateHistory(int direction, ImGuiInputTextCallbackData* data) {
    if (history_.empty()) {
        return;
    }

    const int32_t newCursor = historyCursor_ + direction;
    if (newCursor >= static_cast<int32_t>(history_.size())) {
        return;  // past the oldest entry - stay put
    }
    if (newCursor < -1) {
        return;  // past the draft - stay put
    }

    // Leaving the draft for the first time: snapshot what the user was typing.
    if (historyCursor_ < 0 && newCursor >= 0) {
        draft_.assign(data->Buf, static_cast<size_t>(data->BufTextLen));
    }

    historyCursor_ = newCursor;
    const std::string& replacement = (historyCursor_ < 0) ? draft_ : history_[static_cast<size_t>(historyCursor_)];

    data->DeleteChars(0, data->BufTextLen);
    if (!replacement.empty()) {
        data->InsertChars(0, replacement.c_str());
    }
}

void ConsolePanel::PrefixSearchHistory(int direction, ImGuiInputTextCallbackData* data) {
    if (history_.empty()) {
        return;
    }

    const std::string prefix(data->Buf, static_cast<size_t>(data->CursorPos));
    const int32_t step = direction;

    int32_t i = historyCursor_ + step;
    while (i >= 0 && i < static_cast<int32_t>(history_.size())) {
        const auto& candidate = history_[static_cast<size_t>(i)];
        if (candidate.starts_with(prefix)) {
            // Match. Snapshot the draft on the first hop out of it.
            if (historyCursor_ < 0) {
                draft_.assign(data->Buf, static_cast<size_t>(data->BufTextLen));
            }
            historyCursor_ = i;
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, candidate.c_str());
            // Keep the cursor at the end of the typed prefix so the user
            // can keep typing or refining further.
            data->CursorPos = static_cast<int>(prefix.size());
            return;
        }
        i += step;
    }
    // No match in the requested direction - stay put.
}

int ConsolePanel::InputCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<ConsolePanel*>(data->UserData);
    if (self == nullptr) {
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        // Linear walk. CallbackHistory only fires for Up / Down arrow.
        const int direction = (data->EventKey == ImGuiKey_UpArrow) ? +1 : -1;
        self->NavigateHistory(direction, data);
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp, /*repeat=*/true)) {
            self->PrefixSearchHistory(+1, data);
        } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown, /*repeat=*/true)) {
            self->PrefixSearchHistory(-1, data);
        }
    }
    return 0;
}

std::string ConsolePanel::FormatRow(const Entry& entry, size_t line) const {
    // Input rows carry the "> " prompt; output/error rows are plain. Color codes
    // are stripped so the clipboard gets readable text.
    std::string out = (entry.kind == Kind::Input) ? "> " : "";
    out += StripColor(entry.lines[line]);
    return out;
}

void ConsolePanel::CopyTranscriptToClipboard() const {
    std::string buf;
    buf.reserve(transcript_.size() * 64U);
    for (const auto& entry : transcript_) {
        for (size_t li = 0; li < entry.lines.size(); ++li) {
            buf += FormatRow(entry, li);
            buf += '\n';
        }
    }
    ImGui::SetClipboardText(buf.c_str());
}

void ConsolePanel::CopySelectionToClipboard() const {
    std::string buf;
    for (const auto& entry : transcript_) {
        for (size_t li = 0; li < entry.lines.size(); ++li) {
            if (!selection_.IsSelected(RowId(entry.seq, li))) {
                continue;
            }
            buf += FormatRow(entry, li);
            buf += '\n';
        }
    }
    if (!buf.empty()) {
        ImGui::SetClipboardText(buf.c_str());
    }
}

void ConsolePanel::DrawTranscript() {
    const float footerH = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##transcript", ImVec2(0.0F, -footerH), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        // Auto-scroll only if the user was already at the bottom on entry.
        // Scrolling up disengages it automatically; scrolling back down
        // re-engages on the next frame. Within one line of bottom counts
        // so mouse-wheel ticks don't accidentally drop the pin.
        const float scrollMax = ImGui::GetScrollMaxY();
        const bool atBottom = scrollMax <= 1.0F || ImGui::GetScrollY() >= scrollMax - ImGui::GetTextLineHeight();
        // Don't pin to the bottom while drag-selecting, so the drag's auto-scroll
        // (incl. dragging upward from the bottom) isn't immediately undone.
        const bool autoFollow = atBottom && !selection_.IsDragging();

        // One selectable row per visual line, so multi-line output is
        // line-selectable. Input entries are single-line by construction.
        selection_.Begin();
        for (const auto& entry : transcript_) {
            for (size_t li = 0; li < entry.lines.size(); ++li) {
                if (!selection_.Row(RowId(entry.seq, li))) {
                    continue;  // off-screen: Row() reserved the layout height, skip drawing
                }
                const std::string& line = entry.lines[li];
                switch (entry.kind) {
                    case Kind::Input:
                        ImGui::TextColored(PROMPT_COLOR, "> ");
                        ImGui::SameLine(0.0F, 0.0F);
                        ImGui::TextColored(INPUT_COLOR, "%s", line.c_str());
                        break;
                    case Kind::Output:
                        DrawColoredLine(line);
                        break;
                    case Kind::Error:
                        ImGui::TextColored(ERROR_COLOR, "%s", line.c_str());
                        break;
                }
            }
        }
        selection_.End();
        if (selection_.CopyRequested()) {
            CopySelectionToClipboard();
        }
        if (ImGui::BeginPopupContextWindow("##consolectx")) {
            if (ImGui::MenuItem("Copy selected", "Ctrl+C", false, selection_.HasSelection())) {
                CopySelectionToClipboard();
            }
            if (ImGui::MenuItem("Copy all")) {
                CopyTranscriptToClipboard();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear console")) {
                transcript_.clear();
            }
            ImGui::EndPopup();
        }
        if (autoFollow) {
            ImGui::SetScrollHereY(1.0F);
        }
    }
    ImGui::EndChild();
}

void ConsolePanel::DrawInputLine() {
    if (refocusInput_) {
        ImGui::SetKeyboardFocusHere();
        refocusInput_ = false;
    }
    // Span the full width - clear now lives in the transcript's right-click menu.
    ImGui::SetNextItemWidth(-FLT_MIN);
    constexpr ImGuiInputTextFlags FLAGS =
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways;
    if (ImGui::InputText("##cmd", inputBuf_.data(), inputBuf_.size(), FLAGS, &InputCallback, this)) {
        SubmitInput();
    }
}

void ConsolePanel::Draw() {
    DrawTranscript();
    DrawInputLine();
}

}  // namespace d2bs::js::console
