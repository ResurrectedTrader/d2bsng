#include "components/console/ScriptPanel.h"

#include <fmt/format.h>
#include <imgui.h>
#include <magic_enum/magic_enum.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "components/console/Theme.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::runtime::console {

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

// A figure the engine could not report renders as "-" rather than a zero that
// would read as a measurement.
[[nodiscard]] std::string FormatOptionalBytes(const std::optional<uint64_t>& value) {
    return value ? theme::FormatBytes(*value) : std::string("-");
}

// Render the breakdown body shared between per-script and totals tooltips.
// All fields here are things that actually move under script load: usage
// vs. the limit, how much physical memory is backing the heap, external
// buffers held outside the GC, peak malloced high-water mark, and the
// global-handle pool (where leaks usually show up).
void DrawHeapBreakdown(const HeapStats& stats) {
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
    const double pct = (stats.used && stats.limit && *stats.limit > 0)
                           ? (100.0 * static_cast<double>(*stats.used) / static_cast<double>(*stats.limit))
                           : 0.0;
    row("Used", fmt::format("{} ({:.1f}% of limit)", FormatOptionalBytes(stats.used), pct));
    row("Committed", FormatOptionalBytes(stats.committed));
    row("Physical", FormatOptionalBytes(stats.physical));
    row("Limit", FormatOptionalBytes(stats.limit));
    row("External", FormatOptionalBytes(stats.external));
    row("Peak malloced", FormatOptionalBytes(stats.peakMalloced));
    row("Global handles",
        fmt::format("{} / {}", FormatOptionalBytes(stats.usedHandles), FormatOptionalBytes(stats.totalHandles)));
    ImGui::EndTable();
}

void DrawHeapTooltipBody(const std::optional<HeapStats>& stats) {
    if (!stats) {
        ImGui::TextDisabled("(no heap snapshot yet)");
        return;
    }
    DrawHeapBreakdown(*stats);
}

void Accumulate(std::optional<uint64_t>& total, const std::optional<uint64_t>& value) {
    if (value) {
        total = total.value_or(0) + *value;
    }
}

void AccumulateHeap(HeapStats& total, const HeapStats& value) {
    Accumulate(total.used, value.used);
    Accumulate(total.committed, value.committed);
    Accumulate(total.limit, value.limit);
    Accumulate(total.physical, value.physical);
    Accumulate(total.external, value.external);
    Accumulate(total.peakMalloced, value.peakMalloced);
    Accumulate(total.usedHandles, value.usedHandles);
    Accumulate(total.totalHandles, value.totalHandles);
}

void DrawObjectsTooltipBody(const ObjectCounts& snapshot) {
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

void DrawScriptRow(size_t rowIndex, const std::shared_ptr<Script>& script, HeapStats& heapTotalsOut,
                   int64_t& totalObjectsOut, ObjectCounts& mergedObjectsOut) {
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
        const auto stats = script->GetHeapStats();
        if (stats) {
            AccumulateHeap(heapTotalsOut, *stats);
        }
        if (stats && stats->used) {
            ImGui::TextUnformatted(theme::FormatBytes(*stats->used).c_str());
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
        const auto objectsSnapshot = script->GetObjectCounts();
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

void DrawTotalsRow(const std::vector<std::shared_ptr<Script>>& scripts, const HeapStats& heapTotals,
                   int64_t totalObjects, const ObjectCounts& mergedObjects) {
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
        ImGui::TextUnformatted(theme::FormatBytes(heapTotals.used.value_or(0)).c_str());
        if (hovered) {
            OpenCellTooltip([&] { DrawHeapBreakdown(heapTotals); });
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

    HeapStats heapTotals;
    int64_t totalObjects = 0;
    ObjectCounts mergedObjects;
    std::ranges::sort(scripts, {}, [](const auto& script) { return script->GetName(); });
    for (size_t i = 0; i < scripts.size(); ++i) {
        DrawScriptRow(i, scripts[i], heapTotals, totalObjects, mergedObjects);
    }

    DrawTotalsRow(scripts, heapTotals, totalObjects, mergedObjects);

    ImGui::EndTable();
}

}  // namespace d2bs::runtime::console
