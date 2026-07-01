#include "components/console/ScriptPanel.h"

#include <fmt/format.h>
#include <imgui.h>
#include <v8.h>
#include <magic_enum/magic_enum.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/core/V8InstanceTracker.h"
#include "components/console/Theme.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::js::console {

namespace {

constexpr ImGuiTableFlags TABLE_FLAGS = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

// Hover-test the full table cell, not just the most recently drawn item.
// IsItemHovered only inspects the last item's rect (the text), but the user
// expects mousing over any pixel of the cell to trigger the tooltip. We
// compute the cell's screen rect from the cursor position + remaining
// content region and use raw mouse-rect testing.
[[nodiscard]] bool IsCellHovered() {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetTextLineHeightWithSpacing();
    return ImGui::IsMouseHoveringRect(start, ImVec2(start.x + width, start.y + height));
}

// Render the breakdown body shared between per-script and totals tooltips.
// All fields here are things that actually move under script load: usage
// vs. the limit, how much physical memory is backing the heap, external
// buffers held outside V8's GC, peak malloced high-water mark, and the
// global-handle pool (where leaks usually show up).
void DrawHeapBreakdown(uint64_t used, uint64_t total, uint64_t limit, uint64_t physical, uint64_t external,
                       uint64_t peakMalloced, uint64_t usedHandles, uint64_t totalHandles) {
    if (!ImGui::BeginTable("##heapbreakdown", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        return;
    }
    auto row = [](const char* key, const std::string& value) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(key);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(value.c_str());
    };
    const double pct = (limit > 0) ? (100.0 * static_cast<double>(used) / static_cast<double>(limit)) : 0.0;
    row("Used", fmt::format("{} ({:.1f}% of limit)", theme::FormatBytes(used), pct));
    row("Committed", theme::FormatBytes(total));
    row("Physical", theme::FormatBytes(physical));
    row("Limit", theme::FormatBytes(limit));
    row("External", theme::FormatBytes(external));
    row("Peak malloced", theme::FormatBytes(peakMalloced));
    row("Global handles", fmt::format("{} / {}", theme::FormatBytes(usedHandles), theme::FormatBytes(totalHandles)));
    ImGui::EndTable();
}

void DrawHeapTooltipBody(const std::shared_ptr<v8::HeapStatistics>& stats) {
    if (stats == nullptr) {
        ImGui::TextDisabled("(no heap snapshot yet)");
        return;
    }
    DrawHeapBreakdown(stats->used_heap_size(), stats->total_heap_size(), stats->heap_size_limit(),
                      stats->total_physical_size(), stats->external_memory(), stats->peak_malloced_memory(),
                      stats->used_global_handles_size(), stats->total_global_handles_size());
}

// Sum-of-fields helper for the totals row tooltip.
struct HeapTotals {
    uint64_t used = 0;
    uint64_t total = 0;
    uint64_t limit = 0;
    uint64_t physical = 0;
    uint64_t external = 0;
    uint64_t peakMalloced = 0;
    uint64_t usedHandles = 0;
    uint64_t totalHandles = 0;
};

void DrawObjectsTooltipBody(const api::ClassCountMap& snapshot) {
    if (snapshot.empty()) {
        ImGui::TextDisabled("(no live native objects)");
        return;
    }
    if (ImGui::BeginTable("##objbreakdown", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders)) {
        for (const auto& [name, count] : snapshot) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", count);
        }
        ImGui::EndTable();
    }
}

template <typename BodyFn>
void OpenCellTooltip(BodyFn&& body) {
    ImGui::BeginTooltip();
    std::forward<BodyFn>(body)();
    ImGui::EndTooltip();
}

void DrawScriptRow(size_t rowIndex, const std::shared_ptr<Script>& script, HeapTotals& heapTotalsOut,
                   int64_t& totalObjectsOut, api::ClassCountMap& mergedObjectsOut) {
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(script->GetName().c_str());

    ImGui::TableNextColumn();
    const auto state = script->GetState();
    const std::string stateLabel{magic_enum::enum_name(state)};
    ImGui::TextColored(theme::ColorForState(state), "%s", stateLabel.c_str());

    ImGui::TableNextColumn();
    const std::string modeLabel{magic_enum::enum_name(script->GetMode())};
    ImGui::TextUnformatted(modeLabel.c_str());

    // ----- Heap (cell-wide hover) -----
    ImGui::TableNextColumn();
    {
        const bool hovered = IsCellHovered();
        const auto stats = script->GetCachedHeapStats();
        if (stats != nullptr) {
            const auto used = static_cast<uint64_t>(stats->used_heap_size());
            ImGui::TextUnformatted(theme::FormatBytes(used).c_str());
            heapTotalsOut.used += used;
            heapTotalsOut.total += static_cast<uint64_t>(stats->total_heap_size());
            heapTotalsOut.limit += static_cast<uint64_t>(stats->heap_size_limit());
            heapTotalsOut.physical += static_cast<uint64_t>(stats->total_physical_size());
            heapTotalsOut.external += static_cast<uint64_t>(stats->external_memory());
            heapTotalsOut.peakMalloced += static_cast<uint64_t>(stats->peak_malloced_memory());
            heapTotalsOut.usedHandles += static_cast<uint64_t>(stats->used_global_handles_size());
            heapTotalsOut.totalHandles += static_cast<uint64_t>(stats->total_global_handles_size());
        } else {
            ImGui::TextDisabled("-");
        }
        if (hovered) {
            OpenCellTooltip([&] { DrawHeapTooltipBody(stats); });
        }
    }

    // ----- Objects (cell-wide hover) -----
    ImGui::TableNextColumn();
    {
        const bool hovered = IsCellHovered();
        const auto objectsSnapshot = api::V8InstanceTracker::Instance().Snapshot(script->GetThreadId());
        int32_t objectsTotal = 0;
        for (const auto& [name, count] : objectsSnapshot) {
            objectsTotal += count;
            mergedObjectsOut[name] += count;
        }
        ImGui::Text("%d", objectsTotal);
        totalObjectsOut += objectsTotal;
        if (hovered) {
            OpenCellTooltip([&] { DrawObjectsTooltipBody(objectsSnapshot); });
        }
    }

    // ----- Actions -----
    ImGui::TableNextColumn();
    const bool isConsole = script->GetMode() == ScriptMode::Console;
    const bool canPause = state == ScriptState::Running && !isConsole;
    const bool canResume = state == ScriptState::Paused && !isConsole;

    // Use the row index for the per-row PushID instead of tid - stopped
    // scripts can share a stale tid (e.g. 0 for never-started threads),
    // which would collide PushID(tid) and trip ImGui's id-conflict check.
    ImGui::PushID(static_cast<int>(rowIndex));
    if (isConsole) {
        // Console lifecycle is special - Script::RemoveSelfFromEngine no-ops
        // for console mode (Script.cpp), so raw Stop() leaves the entry in
        // scripts_. RestartConsoleScript stops + erases + respawns properly.
        if (ImGui::SmallButton("Restart")) {
            ScriptEngine::Instance().RestartConsoleScript();
        }
    } else {
        const bool canStop = state == ScriptState::Starting || state == ScriptState::Ready ||
                             state == ScriptState::Running || state == ScriptState::Paused;
        ImGui::BeginDisabled(!canStop);
        if (ImGui::SmallButton("Stop")) {
            script->Stop();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (canResume) {
            if (ImGui::SmallButton("Resume")) {
                script->Resume();
            }
        } else {
            ImGui::BeginDisabled(!canPause);
            if (ImGui::SmallButton("Pause")) {
                script->Pause();
            }
            ImGui::EndDisabled();
        }
    }
    // GC button available wherever the script has an isolate alive - that's
    // anything past Starting, including Paused.
    const bool canGc = state != ScriptState::Stopped;
    ImGui::SameLine();
    ImGui::BeginDisabled(!canGc);
    if (ImGui::SmallButton("GC")) {
        script->RequestGarbageCollection();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void DrawTotalsRow(const std::vector<std::shared_ptr<Script>>& scripts, const HeapTotals& heapTotals,
                   int64_t totalObjects, const api::ClassCountMap& mergedObjects) {
    int32_t pausableCount = 0;
    int32_t resumableCount = 0;
    for (const auto& script : scripts) {
        if (script->GetMode() == ScriptMode::Console) {
            continue;  // mass actions ignore the console
        }
        if (script->GetState() == ScriptState::Running) {
            ++pausableCount;
        } else if (script->GetState() == ScriptState::Paused) {
            ++resumableCount;
        }
    }

    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextDisabled("Totals (%zu)", scripts.size());

    ImGui::TableNextColumn();
    ImGui::TableNextColumn();

    // Heap totals - sum of used across all scripts, with hover for the
    // full breakdown summed across isolates.
    ImGui::TableNextColumn();
    {
        const bool hovered = IsCellHovered();
        ImGui::TextUnformatted(theme::FormatBytes(heapTotals.used).c_str());
        if (hovered) {
            OpenCellTooltip([&] {
                DrawHeapBreakdown(heapTotals.used, heapTotals.total, heapTotals.limit, heapTotals.physical,
                                  heapTotals.external, heapTotals.peakMalloced, heapTotals.usedHandles,
                                  heapTotals.totalHandles);
            });
        }
    }

    // Objects totals - hover to see merged per-type breakdown across all scripts.
    ImGui::TableNextColumn();
    {
        const bool hovered = IsCellHovered();
        ImGui::Text("%lld", static_cast<long long>(totalObjects));
        if (hovered) {
            OpenCellTooltip([&] { DrawObjectsTooltipBody(mergedObjects); });
        }
    }

    ImGui::TableNextColumn();
    ImGui::PushID("##totalsactions");
    if (ImGui::SmallButton("Stop all")) {
        ScriptEngine::Instance().StopAllScripts();
    }
    if (pausableCount > 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Pause all")) {
            for (const auto& script : scripts) {
                if (script->GetMode() != ScriptMode::Console && script->GetState() == ScriptState::Running) {
                    script->Pause();
                }
            }
        }
    }
    if (resumableCount > 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Resume all")) {
            for (const auto& script : scripts) {
                if (script->GetMode() != ScriptMode::Console && script->GetState() == ScriptState::Paused) {
                    script->Resume();
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("GC all")) {
        for (const auto& script : scripts) {
            script->RequestGarbageCollection();
        }
    }
    ImGui::PopID();
}

}  // namespace

void ScriptPanel::Draw() {
    auto scripts = ScriptEngine::Instance().GetAllScripts();
    if (scripts.empty()) {
        ImGui::TextDisabled("No scripts running.");
        return;
    }

    if (!ImGui::BeginTable("##scripts", 6, TABLE_FLAGS)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("State");
    ImGui::TableSetupColumn("Mode");
    ImGui::TableSetupColumn("Heap");
    ImGui::TableSetupColumn("Objects");
    // Fixed width sized for the totals row (Stop all | Pause all | Resume all | GC all);
    // the per-script row's narrower set of buttons fits inside this comfortably.
    // CalcTextSize keeps it font/DPI-relative; the trailing pad covers button frames + item spacing.
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                            ImGui::CalcTextSize("Stop all  Pause all  Resume all  GC all    ").x);
    ImGui::TableHeadersRow();

    HeapTotals heapTotals;
    int64_t totalObjects = 0;
    api::ClassCountMap mergedObjects;
    std::ranges::sort(scripts, {}, [](const auto& script) { return script->GetName(); });
    for (size_t i = 0; i < scripts.size(); ++i) {
        DrawScriptRow(i, scripts[i], heapTotals, totalObjects, mergedObjects);
    }

    DrawTotalsRow(scripts, heapTotals, totalObjects, mergedObjects);

    ImGui::EndTable();
}

}  // namespace d2bs::js::console
