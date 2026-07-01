#include "components/console/StacktracesPanel.h"

#include <fmt/format.h>
#include <imgui.h>
#include <v8.h>
#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::js::console {

namespace {

constexpr ImGuiTableFlags STACK_TABLE_FLAGS =
    ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

void DrawStackTable(const std::vector<StackFrame>& frames) {
    if (frames.empty()) {
        ImGui::TextDisabled("(no JS frames on stack - script is between events)");
        return;
    }
    if (!ImGui::BeginTable("##stack", 4, STACK_TABLE_FLAGS)) {
        return;
    }
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0F);
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch, 0.40F);
    ImGui::TableSetupColumn("Script", ImGuiTableColumnFlags_WidthStretch, 0.55F);
    ImGui::TableSetupColumn("Line:Col", ImGuiTableColumnFlags_WidthFixed, 80.0F);
    ImGui::TableHeadersRow();

    // Render outermost-first (frame N-1 down to 0). The deepest frame is
    // the "active" one - putting it at the bottom of the table keeps the
    // stable outer frames pinned at the top so the eye doesn't have to
    // chase shifting rows as call depth fluctuates. The "#" column still
    // shows V8's native frame index (0 = innermost / current), so the
    // index -> meaning convention is preserved.
    for (size_t k = 0; k < frames.size(); ++k) {
        const size_t i = frames.size() - 1 - k;
        const auto& f = frames[i];
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::Text("%zu", i);

        ImGui::TableNextColumn();
        if (f.functionName.empty()) {
            ImGui::TextDisabled("<anonymous>");
        } else {
            ImGui::TextUnformatted(f.functionName.c_str());
        }

        ImGui::TableNextColumn();
        if (f.scriptName.empty()) {
            ImGui::TextDisabled("<unknown>");
        } else {
            ImGui::TextUnformatted(f.scriptName.c_str());
        }

        ImGui::TableNextColumn();
        ImGui::Text("%d:%d", f.line, f.column);
    }

    ImGui::EndTable();
}

}  // namespace

void StacktracesPanel::Draw() {
    const auto scripts = ScriptEngine::Instance().GetAllScripts();

    std::shared_ptr<Script> selected;
    if (selectedTid_ != 0) {
        const auto it = std::ranges::find_if(
            scripts, [tid = selectedTid_](const auto& s) { return s->GetNativeThreadId() == tid; });
        if (it != scripts.end()) {
            selected = *it;
        }
    }
    if (selectedTid_ != 0 && selected == nullptr) {
        selectedTid_ = 0;  // selection vanished (script stopped)
    }

    const std::string previewLabel = (selected != nullptr) ? selected->GetName() : std::string{"(none)"};

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Script:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0F);
    // Invariant: at most one script has capture enabled (OnYield/OnEveryCall) at
    // any time - whichever is `selected`. Both selection-changing branches below
    // clear the previous script's mode before reassigning, so navigating away
    // can't leak capture cost. A script that stops while selected keeps its mode
    // (harmless - it isn't running); console hide also clears all capture.
    if (ImGui::BeginCombo("##script", previewLabel.c_str())) {
        if (ImGui::Selectable("(none)", selectedTid_ == 0)) {
            if (selected != nullptr) {
                selected->SetStackCaptureMode(StackCaptureMode::Off);
            }
            selectedTid_ = 0;
            selected.reset();
        }
        for (size_t i = 0; i < scripts.size(); ++i) {
            const auto& s = scripts[i];
            const uint32_t tid = s->GetNativeThreadId();
            // Disambiguating "##i" suffix because multiple scripts can share
            // a name (e.g. several stopped consoles) and ImGui keys Selectable
            // identity by visible label otherwise.
            const std::string label = fmt::format("{} [{}]##{}", s->GetName(), magic_enum::enum_name(s->GetState()), i);
            if (ImGui::Selectable(label.c_str(), tid == selectedTid_ && selected.get() == s.get())) {
                if (selected != nullptr && selected.get() != s.get()) {
                    selected->SetStackCaptureMode(StackCaptureMode::Off);
                }
                selectedTid_ = tid;
                selected = s;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Capture every native call", &captureOnEveryCall_)) {
        // Toggling only touches the selected script; unselected ones are already Off.
        if (selected != nullptr) {
            selected->SetStackCaptureMode(captureOnEveryCall_ ? StackCaptureMode::OnEveryCall
                                                              : StackCaptureMode::OnYield);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: refresh the snapshot at this script's delay() yields (cheap).\n"
                          "On: also refresh on every JS->native callback - live, but adds a stack-walk to\n"
                          "every method / property / function call from this script.");
    }

    if (selected == nullptr) {
        ImGui::Spacing();
        ImGui::TextDisabled("Pick a script to view its JS call stack - it captures at delay() yields while");
        ImGui::TextDisabled("selected; flip the checkbox above for a live every-native-call view.");
        return;
    }

    // Re-assert the selected script's capture tier each frame (handles the
    // checkbox toggle and reselection).
    selected->SetStackCaptureMode(captureOnEveryCall_ ? StackCaptureMode::OnEveryCall : StackCaptureMode::OnYield);

    if (const auto snapshot = selected->GetLastStackTrace(); snapshot != nullptr) {
        DrawStackTable(snapshot->frames);
    } else {
        ImGui::TextDisabled("(no capture yet - script hasn't yielded or made a tracked native call)");
    }
}

}  // namespace d2bs::js::console
