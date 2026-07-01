#include "components/console/ThreadsPanel.h"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "utils/threadutils.h"

namespace d2bs::js::console {

namespace {

struct ThreadEntry {
    uint32_t tid = 0;
    std::string description;
};

[[nodiscard]] std::vector<ThreadEntry> EnumerateThreads() {
    auto tids = thread_utils::EnumerateProcessThreads();
    std::ranges::sort(tids);
    std::vector<ThreadEntry> result;
    result.reserve(tids.size());
    for (uint32_t tid : tids) {
        result.push_back({.tid = tid, .description = thread_utils::GetThreadDescription(tid)});
    }
    return result;
}

[[nodiscard]] std::string FormatLabel(const ThreadEntry& entry) {
    if (entry.description.empty()) {
        return fmt::format("tid {} ({:#x})", entry.tid, entry.tid);
    }
    return fmt::format("tid {} ({:#x}) - {}", entry.tid, entry.tid, entry.description);
}

}  // namespace

void ThreadsPanel::Draw() {
    const auto threads = EnumerateThreads();

    const auto it = std::ranges::find_if(threads, [tid = selectedTid_](const ThreadEntry& e) { return e.tid == tid; });
    const auto* selected = (it != threads.end()) ? &*it : nullptr;

    const std::string previewLabel = (selected != nullptr) ? FormatLabel(*selected) : std::string{"(none)"};

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Thread:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(480.0F);
    if (ImGui::BeginCombo("##thread", previewLabel.c_str())) {
        if (ImGui::Selectable("(none)", selectedTid_ == 0)) {
            selectedTid_ = 0;
        }
        for (const auto& entry : threads) {
            const bool isSelected = entry.tid == selectedTid_;
            if (ImGui::Selectable(FormatLabel(entry).c_str(), isSelected)) {
                selectedTid_ = entry.tid;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const bool canCapture = selected != nullptr;
    ImGui::BeginDisabled(!canCapture);
    if (ImGui::Button("Capture")) {
        capturedTid_ = selected->tid;
        capturedDescription_ = selected->description;
        capturedStack_ = thread_utils::GetThreadStacktrace(selected->tid, 0);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    if (capturedStack_.empty()) {
        ImGui::TextDisabled("Pick a thread and press 'Capture' to walk its stack.");
        return;
    }

    if (capturedDescription_.empty()) {
        ImGui::Text("Captured tid %u (0x%X)", capturedTid_, capturedTid_);
    } else {
        ImGui::Text("Captured tid %u (0x%X) - %s", capturedTid_, capturedTid_, capturedDescription_.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(capturedStack_.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        capturedStack_.clear();
        capturedDescription_.clear();
        capturedTid_ = 0;
        return;
    }

    ImGui::Spacing();
    // ReadOnly multiline input doubles as a copy-friendly viewer; users can
    // select-and-copy a subset directly too.
    constexpr ImGuiInputTextFlags FLAGS = ImGuiInputTextFlags_ReadOnly;
    ImGui::InputTextMultiline("##stack", capturedStack_.data(), capturedStack_.size() + 1, ImVec2(-FLT_MIN, -FLT_MIN),
                              FLAGS);
}

}  // namespace d2bs::js::console
