#include "components/console/LogPanel.h"

#include <fmt/format.h>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "components/console/Theme.h"
#include "utils/utils.h"

namespace d2bs::js::console {

namespace {

using game::console::Message;
using game::console::MessageLevel;
using game::console::MessageSource;

constexpr std::array ALL_LEVELS = {
    MessageLevel::Trace, MessageLevel::Debug, MessageLevel::Info,
    MessageLevel::Warn,  MessageLevel::Error, MessageLevel::Critical,
};

// Strip everything up to and including the last path separator. Used in
// the rendered prefix so scripts logged as "libs\SoloPlay\SoloPlay.js" show
// as "SoloPlay.js". The full name is still kept on the Entry for the
// Sources filter (otherwise two scripts with the same filename in different
// directories would collide).
[[nodiscard]] std::string_view Basename(std::string_view name) {
    const auto pos = name.find_last_of("/\\");
    return pos == std::string_view::npos ? name : name.substr(pos + 1);
}

[[nodiscard]] std::string SummariseCombo(size_t on, size_t total) {
    if (on == total) {
        return "All";
    }
    if (on == 0) {
        return "None";
    }
    return fmt::format("{}/{}", on, total);
}

}  // namespace

void LogPanel::Append(Message msg) {
    // Record the script name so the filter UI sees it. emplace returns
    // {it, inserted}; we don't care which - the default value of `true`
    // sticks only on first insert.
    if (!msg.name.empty()) {
        scriptVisibility_.try_emplace(msg.name, true);
    }
    scrollback_.push_back(Entry{
        .msg = std::move(msg),
        .ts = std::chrono::system_clock::now(),
        .seq = nextSeq_++,
    });
    while (scrollback_.size() > MAX_SCROLLBACK) {
        scrollback_.pop_front();
    }
}

bool LogPanel::SourceVisible(MessageSource source) const {
    for (const auto& filter : sourceFilters_) {
        if (filter.source == source) {
            return filter.visible;
        }
    }
    return false;  // unknown source - hide defensively (should not happen)
}

bool LogPanel::MatchesFilter(const Entry& entry) const {
    if (!SourceVisible(entry.msg.source)) {
        return false;
    }
    if (static_cast<uint8_t>(entry.msg.level) < static_cast<uint8_t>(minLevel_)) {
        return false;
    }
    if (!entry.msg.name.empty()) {
        if (auto it = scriptVisibility_.find(entry.msg.name); it != scriptVisibility_.end() && !it->second) {
            return false;
        }
    }
    const std::string_view textFilter(textFilter_.data());
    return textFilter.empty() || utils::ContainsCaseInsensitive(entry.msg.text, textFilter);
}

void LogPanel::DrawFilterBar() {
    // ----- Kind combo (filter-only; the rendered prefix does not include source) -----
    {
        size_t on = 0;
        for (const auto& filter : sourceFilters_) {
            if (filter.visible) {
                ++on;
            }
        }
        // AlignTextToFramePadding shifts the cursor down by FramePadding.y
        // so the bare label text sits on the baseline shared by the framed
        // widgets that follow it on the same row.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Kind:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0F);
        if (ImGui::BeginCombo("##kind", SummariseCombo(on, sourceFilters_.size()).c_str())) {
            for (auto& filter : sourceFilters_) {
                const std::string tag{theme::SourceTag(filter.source)};
                ImGui::Checkbox(tag.c_str(), &filter.visible);
            }
            ImGui::EndCombo();
        }
    }

    // ----- Min-level combo -----
    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Min level:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0F);
        const std::string levelLabel{theme::LevelTag(minLevel_)};
        if (ImGui::BeginCombo("##minlevel", levelLabel.c_str())) {
            for (auto lv : ALL_LEVELS) {
                const std::string tag{theme::LevelTag(lv)};
                const bool selected = (lv == minLevel_);
                if (ImGui::Selectable(tag.c_str(), selected)) {
                    minLevel_ = lv;
                }
            }
            ImGui::EndCombo();
        }
    }

    // ----- Sources combo -----
    {
        ImGui::SameLine();
        size_t on = 0;
        for (const auto& [_, v] : scriptVisibility_) {
            if (v) {
                ++on;
            }
        }
        const std::string label =
            scriptVisibility_.empty() ? std::string{"(none yet)"} : SummariseCombo(on, scriptVisibility_.size());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Sources:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0F);
        if (ImGui::BeginCombo("##sources", label.c_str())) {
            if (scriptVisibility_.empty()) {
                ImGui::TextDisabled("No sources have logged yet.");
            } else {
                if (ImGui::SmallButton("All on")) {
                    for (auto& [_, v] : scriptVisibility_) {
                        v = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("All off")) {
                    for (auto& [_, v] : scriptVisibility_) {
                        v = false;
                    }
                }
                ImGui::Separator();
                for (auto& [name, visible] : scriptVisibility_) {
                    ImGui::Checkbox(name.c_str(), &visible);
                }
            }
            ImGui::EndCombo();
        }
    }

    // ----- Text substring -----
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    ImGui::InputTextWithHint("##textFilter", "substring...", textFilter_.data(), textFilter_.size());
}

std::string LogPanel::FormatEntry(const Entry& entry) const {
    return fmt::format("[{}] [{}] [{}] {}\n", theme::FormatTimestamp(entry.ts),
                       entry.msg.name.empty() ? std::string_view{"d2bs"} : Basename(entry.msg.name),
                       theme::LevelTag(entry.msg.level), game::console::StripColor(entry.msg.text));
}

void LogPanel::CopyViewToClipboard() const {
    std::string buf;
    buf.reserve(scrollback_.size() * 96U);
    for (const auto& entry : scrollback_) {
        if (!MatchesFilter(entry)) {
            continue;
        }
        buf += FormatEntry(entry);
    }
    ImGui::SetClipboardText(buf.c_str());
}

void LogPanel::CopySelectionToClipboard() const {
    std::string buf;
    for (const auto& entry : scrollback_) {
        if (!MatchesFilter(entry) || !selection_.IsSelected(entry.seq)) {
            continue;
        }
        buf += FormatEntry(entry);
    }
    if (!buf.empty()) {
        ImGui::SetClipboardText(buf.c_str());
    }
}

void LogPanel::DrawScrollback() {
    constexpr ImVec4 PREFIX_COLOR{0.55F, 0.55F, 0.55F, 1.00F};
    if (ImGui::BeginChild("##scroll", ImVec2(0.0F, 0.0F), false, ImGuiWindowFlags_HorizontalScrollbar)) {
        // Auto-scroll only while the user was already at (or near) the
        // bottom on entry to this frame. Scrolling up disengages it
        // automatically; scrolling back down re-engages on the next frame.
        const float scrollMax = ImGui::GetScrollMaxY();
        const bool atBottom = scrollMax <= 1.0F || ImGui::GetScrollY() >= scrollMax - ImGui::GetTextLineHeight();
        // Don't pin to the bottom while drag-selecting, so the drag's auto-scroll
        // (incl. dragging upward from the bottom) isn't immediately undone.
        const bool autoFollow = atBottom && !selection_.IsDragging();

        selection_.Begin();
        for (const auto& entry : scrollback_) {
            if (!MatchesFilter(entry)) {
                continue;
            }
            if (!selection_.Row(entry.seq)) {
                continue;  // off-screen: Row() reserved the layout height, skip formatting
            }
            const std::string prefix =
                fmt::format("[{}] [{}] [{}] ", theme::FormatTimestamp(entry.ts),
                            entry.msg.name.empty() ? std::string_view{"d2bs"} : Basename(entry.msg.name),
                            theme::LevelTag(entry.msg.level));
            ImGui::TextColored(PREFIX_COLOR, "%s", prefix.c_str());

            for (const auto& seg : game::console::SplitByColor(entry.msg.text)) {
                if (seg.text.empty()) {
                    continue;
                }
                ImGui::SameLine(0.0F, 0.0F);
                ImGui::TextColored(theme::ColorForCode(seg.colorCode), "%s", seg.text.c_str());
            }
        }
        selection_.End();
        if (selection_.CopyRequested()) {
            CopySelectionToClipboard();
        }
        if (ImGui::BeginPopupContextWindow("##logctx")) {
            if (ImGui::MenuItem("Copy selected", "Ctrl+C", false, selection_.HasSelection())) {
                CopySelectionToClipboard();
            }
            if (ImGui::MenuItem("Copy all")) {
                CopyViewToClipboard();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear log")) {
                scrollback_.clear();
            }
            ImGui::EndPopup();
        }
        if (autoFollow) {
            ImGui::SetScrollHereY(1.0F);
        }
    }
    ImGui::EndChild();
}

void LogPanel::Draw() {
    DrawFilterBar();
    ImGui::Separator();
    DrawScrollback();
}

}  // namespace d2bs::js::console
