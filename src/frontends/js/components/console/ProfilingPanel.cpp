#include "components/console/ProfilingPanel.h"

#include <imgui.h>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "utils/threadutils.h"

#ifdef D2BS_PROFILING

namespace d2bs::js::console {

namespace {

// The measurement window. Must clear one ~16ms game frame comfortably, and be slow enough that the
// numbers hold still to read.
constexpr double SAMPLE_SECONDS = 0.5;

// Threads below this peak stay hidden unless "show all" is ticked.
constexpr double INTERESTING_CPU_PERCENT = 0.05;

// History horizon, in windows. A thread busy for one second in ten averages under 10% and reads as
// idle when sampled at the wrong moment; the trace and the peaks show the shape.
constexpr size_t HISTORY_SAMPLES = 120;
constexpr float HISTORY_WIDTH = 120.0F;
constexpr float HISTORY_HEIGHT = 16.0F;
// One vertical scale for every plot, floored so an all-idle process does not magnify noise.
constexpr float HISTORY_SCALE_FLOOR = 10.0F;

// The native-call table is a "what is expensive" list, so it is capped rather than scrolled.
constexpr size_t TOP_BINDINGS = 20;
constexpr size_t TOP_BLOCKED_BINDINGS = 5;
constexpr float BINDING_TABLE_HEIGHT = 180.0F;

// Below this the panel scrolls rather than squeezing the thread table away.
constexpr float THREAD_TABLE_MIN_HEIGHT = 160.0F;

// Timelines with a lower TimelineInfo::order draw before the panel's own sections, the rest after.
constexpr int32_t OWN_SECTIONS_ORDER = 100;

constexpr ImGuiTableFlags SORTABLE_TABLE_FLAGS = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                                                 ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                                 ImGuiTableFlags_Sortable;

// Numeric columns sort descending on the first click: the question is always "what is biggest".
constexpr ImGuiTableColumnFlags NUMERIC_COLUMN =
    ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending;

constexpr std::array<const char*, profiling::NATIVE_CALL_KINDS> KIND_LABELS = {"fn", "get", "set"};

// Column indices, shared between the header setup and the sort comparator.
enum ThreadColumn : int32_t {
    ThreadColTid = 0,
    ThreadColCpu,
    ThreadColPeak,
    ThreadColNative,
    ThreadColJs,
    ThreadColPeakNative,
    ThreadColPeakJs,
    ThreadColWakes,
    ThreadColAwake,
    ThreadColHistory,
    ThreadColName,
    ThreadColCount,
};

enum BindingColumn : int32_t {
    BindingColShare = 0,
    BindingColKind,
    BindingColCalls,
    BindingColCycles,
    BindingColPerCall,
    BindingColBlocked,
    BindingColName,
    BindingColCount,
};

// Thousands separators, locale-independent.
[[nodiscard]] std::string Grouped(uint64_t value) {
    std::string out = std::to_string(value);
    for (auto i = static_cast<std::ptrdiff_t>(out.size()) - 3; i > 0; i -= 3) {
        out.insert(static_cast<size_t>(i), ",");
    }
    return out;
}

[[nodiscard]] std::string FormatDuration(double seconds) {
    const auto total = static_cast<uint64_t>(seconds > 0.0 ? seconds : 0.0);
    const uint64_t hours = total / 3600;
    const uint64_t minutes = (total % 3600) / 60;
    const uint64_t secs = total % 60;
    if (hours > 0) {
        return fmt::format("{}h {:02}m {:02}s", hours, minutes, secs);
    }
    if (minutes > 0) {
        return fmt::format("{}m {:02}s", minutes, secs);
    }
    return fmt::format("{}s", secs);
}

// Reads the table's sort state, returning true when the caller must re-sort. Column -1 means the
// user cleared the sort and the table's own default order applies.
[[nodiscard]] bool TakeSortSpec(int32_t& column, bool& ascending) {
    auto* specs = ImGui::TableGetSortSpecs();
    if (specs == nullptr || !specs->SpecsDirty) {
        return false;
    }
    if (specs->SpecsCount > 0) {
        column = specs->Specs[0].ColumnIndex;
        ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    } else {
        column = -1;
    }
    specs->SpecsDirty = false;
    return true;
}

template <typename Row, typename Key>
void SortRows(std::vector<Row>& rows, bool ascending, Key key) {
    if (ascending) {
        std::ranges::sort(rows, std::ranges::less{}, key);
    } else {
        std::ranges::sort(rows, std::ranges::greater{}, key);
    }
}

// For columns that show a dash on some rows: rows without a value sink to the bottom whichever way
// the column is sorted, instead of interleaving with the genuine zeros.
template <typename Row, typename Key, typename Present>
void SortRows(std::vector<Row>& rows, bool ascending, Key key, Present present) {
    std::ranges::sort(rows, [&](const Row& a, const Row& b) {
        const bool aPresent = std::invoke(present, a);
        const bool bPresent = std::invoke(present, b);
        if (aPresent != bPresent) {
            return aPresent;
        }
        return ascending ? std::invoke(key, a) < std::invoke(key, b) : std::invoke(key, a) > std::invoke(key, b);
    });
}

// Counters are cumulative and a thread can leave (or a tid be reused) between windows.
[[nodiscard]] uint64_t Delta(uint64_t now, uint64_t before) {
    return now > before ? now - before : 0;
}

[[nodiscard]] double Percent(uint64_t part, uint64_t total) {
    return total == 0 ? 0.0 : (100.0 * static_cast<double>(part) / static_cast<double>(total));
}

[[nodiscard]] double PerCall(uint64_t total, uint64_t calls) {
    return calls == 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(calls);
}

// Cycles as a time, at the TSC rate the last window measured. Raw cycles until there is one.
[[nodiscard]] std::string FormatCycles(double cycles, double tscHz) {
    if (tscHz <= 0.0) {
        return fmt::format("{:.0f} cyc", cycles);
    }
    const double micros = cycles / tscHz * 1e6;
    if (micros < 1000.0) {
        return fmt::format("{:.1f} us", micros);
    }
    if (micros < 1e6) {
        return fmt::format("{:.2f} ms", micros / 1000.0);
    }
    return fmt::format("{:.2f} s", micros / 1e6);
}

[[nodiscard]] const char* KindLabel(profiling::NativeCall kind) {
    return KIND_LABELS.at(static_cast<size_t>(kind));
}

// Colour a percentage by how much it should worry the reader; no thresholds leaves it plain.
void TextPercent(double value, double warn = 0.0, double bad = 0.0) {
    if (bad <= 0.0) {
        ImGui::Text("%.1f%%", value);
        return;
    }
    ImVec4 color(0.6F, 0.85F, 0.6F, 1.0F);
    if (value >= bad) {
        color = ImVec4(1.0F, 0.4F, 0.4F, 1.0F);
    } else if (value >= warn) {
        color = ImVec4(1.0F, 0.85F, 0.4F, 1.0F);
    }
    ImGui::TextColored(color, "%.1f%%", value);
}

template <typename Draw>
void OrDash(bool show, Draw draw) {
    if (show) {
        draw();
    } else {
        ImGui::TextDisabled("-");
    }
}

// Appends to a fixed-length series, dropping the oldest sample.
void Append(std::vector<float>& series, double value) {
    if (series.size() == HISTORY_SAMPLES) {
        series.erase(series.begin());
    }
    series.push_back(static_cast<float>(value));
}

[[nodiscard]] double Peak(const std::vector<float>& series) {
    return series.empty() ? 0.0 : *std::ranges::max_element(series);
}

}  // namespace

void ProfilingPanel::Draw() {
    const auto now = std::chrono::steady_clock::now();
    const double sinceDraw = lastDraw_ == std::chrono::steady_clock::time_point{}
                                 ? 0.0
                                 : std::chrono::duration<double>(now - lastDraw_).count();
    lastDraw_ = now;

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Rates are measured over %.1fs windows while this panel is open. CPU%% is of one core; a "
                       "thread that is asleep costs nothing regardless of how long it waits.",
                       SAMPLE_SECONDS);
    ImGui::PopStyleColor();
    ImGui::Checkbox("Show all threads", &showAllThreads_);
    ImGui::SameLine();
    bool timeNativeCalls = profiling::nativeTimingEnabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Time native calls", &timeNativeCalls)) {
        profiling::nativeTimingEnabled.store(timeNativeCalls, std::memory_order_relaxed);
        if (timeNativeCalls) {
            collectStart_ = now;
        } else if (collecting_) {
            collected_ += now - collectStart_;
        }
        collecting_ = timeNativeCalls;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Times every JS->native crossing, split by function / getter / setter and\n"
                          "attributed per binding. Off by default: property reads go through the same\n"
                          "path, so this is two clock reads against callbacks that can be shorter than\n"
                          "that themselves.");
    }
    constexpr const char* RESET_PEAKS = "Reset peaks";
    const float resetWidth = ImGui::CalcTextSize(RESET_PEAKS).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - resetWidth);
    if (ImGui::SmallButton(RESET_PEAKS)) {
        for (auto& [title, state] : timelineState_) {
            state.ours.clear();
        }
        for (auto& [tid, state] : threadState_) {
            state.cpu.clear();
            state.native.clear();
            state.js.clear();
        }
    }
    ImGui::Spacing();

    sinceSample_ += sinceDraw;
    if (sinceSample_ >= SAMPLE_SECONDS) {
        // The window in TSC cycles is the denominator of every CPU share. The TSC is invariant and
        // synchronised across cores on anything this runs on.
        const uint64_t cycles = profiling::Cycles();
        const bool baseline = prevSampleCycles_ == 0;
        const uint64_t windowCycles = baseline ? 0 : Delta(cycles, prevSampleCycles_);
        prevSampleCycles_ = cycles;
        if (windowCycles > 0) {
            tscHz_ = static_cast<double>(windowCycles) / sinceSample_;
        }

        SampleTimelines(sinceSample_);
        SampleThreads(sinceSample_, windowCycles);
        SampleNativeBindings();
        // The first window only establishes the counters to diff against.
        haveSample_ = !baseline;
        sinceSample_ = 0.0;
    }

    if (!haveSample_) {
        ImGui::TextDisabled("Sampling...");
        return;
    }

    // One scrolling region, so a short console scrolls instead of squeezing the thread table away.
    if (!ImGui::BeginChild("##sections")) {
        ImGui::EndChild();
        return;
    }

    // Each section folds under a header that carries its headline figure, so a folded section is
    // still a one-line summary and the ones under investigation get the room.
    auto view = timelines_.begin();
    for (; view != timelines_.end() && view->info->order < OWN_SECTIONS_ORDER; ++view) {
        DrawTimeline(*view);
        ImGui::Spacing();
    }
    DrawNativeCalls();
    ImGui::Spacing();
    DrawThreads();
    for (; view != timelines_.end(); ++view) {
        ImGui::Spacing();
        DrawTimeline(*view);
    }

    ImGui::EndChild();
}

void ProfilingPanel::SampleTimelines(double window) {
    auto samples = profiling::SnapshotTimelines();

    std::vector<TimelineView> views;
    std::unordered_map<std::string, TimelineState> next;
    views.reserve(samples.size());

    for (auto& sample : samples) {
        const profiling::TimelineInfo& info = *sample.info;
        TimelineView view{.info = &info, .detail = std::move(sample.detail)};
        view.cycles.assign(sample.phases.size(), 0);

        // Carried forward so a one-off stall stays visible as the peak; the map is rebuilt from
        // what is registered now, which is what prunes a title that has gone.
        TimelineState state;
        if (auto it = timelineState_.find(info.title); it != timelineState_.end()) {
            state = std::move(it->second);
        }

        if (state.prev.size() == sample.phases.size()) {
            for (size_t i = 0; i < sample.phases.size(); ++i) {
                view.cycles[i] = Delta(sample.phases[i].cycles, state.prev[i].cycles);
            }
            if (info.framePhase < sample.phases.size()) {
                const uint64_t frames =
                    Delta(sample.phases[info.framePhase].entries, state.prev[info.framePhase].entries);
                view.framesPerSecond = static_cast<double>(frames) / window;
            }
        }

        uint64_t total = 0;
        uint64_t ours = 0;
        for (size_t i = 0; i < view.cycles.size(); ++i) {
            total += view.cycles[i];
            if (!info.phases[i].foreign && !info.phases[i].blocking) {
                ours += view.cycles[i];
            }
            view.hasForeign = view.hasForeign || info.phases[i].foreign;
        }
        view.oursPercent = Percent(ours, total);
        if (view.framesPerSecond > 0.0) {
            Append(state.ours, view.oursPercent);
        }
        view.peakOursPercent = Peak(state.ours);

        state.prev = std::move(sample.phases);
        next.emplace(info.title, std::move(state));
        views.push_back(std::move(view));
    }
    std::ranges::stable_sort(views, {}, [](const TimelineView& v) { return v.info->order; });
    timelineState_ = std::move(next);
    timelines_ = std::move(views);
}

void ProfilingPanel::DrawTimeline(const TimelineView& view) {
    const profiling::TimelineInfo& info = *view.info;

    // "###" keeps the header's ID (and so its open state) fixed while the figures in it change.
    std::string header = info.title;
    if (view.framesPerSecond > 0.0) {
        header += fmt::format("  {:.1f} {}/s", view.framesPerSecond, info.frameLabel);
        // Our share only means something against someone else's loop.
        if (view.hasForeign) {
            header += fmt::format(", ours {:.1f}% (peak {:.1f}%)", view.oursPercent, view.peakOursPercent);
        }
    }
    header += fmt::format("###{}", info.title);
    if (!ImGui::CollapsingHeader(header.c_str(), info.expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        return;
    }
    if (view.hasForeign && ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "\"ours\" is our non-blocking phases as a share of the loop; the peak is over the last %.0fs.",
            static_cast<double>(HISTORY_SAMPLES) * SAMPLE_SECONDS);
    }

    if (!view.detail.empty()) {
        ImGui::TextDisabled("%s", view.detail.c_str());
    }
    if (view.framesPerSecond == 0.0) {
        ImGui::TextDisabled("%s", info.idle);
        return;
    }

    // Shares are of the loop's whole elapsed time, foreign and asleep phases included.
    uint64_t total = 0;
    for (const uint64_t cycles : view.cycles) {
        total += cycles;
    }

    ImGui::PushID(info.title);
    if (ImGui::BeginTable("##timeline", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Segment", ImGuiTableColumnFlags_WidthFixed, 190.0F);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, 70.0F);
        ImGui::TableSetupColumn("What it is");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < view.cycles.size(); ++i) {
            const profiling::PhaseInfo& phase = info.phases[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(phase.name);
            ImGui::TableNextColumn();
            TextPercent(Percent(view.cycles[i], total), phase.warn, phase.bad);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", phase.what);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void ProfilingPanel::SampleThreads(double window, uint64_t windowCycles) {
    const std::vector<profiling::ThreadSample> threads = profiling::SnapshotThreads();

    std::vector<Row> rows;
    std::unordered_map<uint32_t, ThreadState> next;
    next.reserve(threads.size());
    double totalCpu = 0.0;

    for (const auto& current : threads) {
        const uint32_t tid = current.tid;
        Row row{.tid = tid};

        // Rebuilding the map from the live threads is what prunes exited ones, and a reused tid
        // starts from an empty trace.
        ThreadState state;
        const auto it = threadState_.find(tid);
        const bool seen = it != threadState_.end();
        if (seen) {
            state = std::move(it->second);
            const profiling::ThreadSample& prev = state.prev;
            row.cpuPercent = Percent(Delta(current.cpuCycles, prev.cpuCycles), windowCycles);
            row.wakesPerSecond = static_cast<double>(Delta(current.wakeups, prev.wakeups)) / window;
            const uint64_t work = Delta(current.workCycles, prev.workCycles);
            const uint64_t slept = Delta(current.sleptCycles, prev.sleptCycles);
            row.awakePercent = Percent(work, work + slept);
            row.hasLoopCounters = (work + slept) > 0;

            for (size_t kind = 0; kind < profiling::NATIVE_CALL_KINDS; ++kind) {
                const uint64_t cycles = Delta(current.native.at(kind).cycles, prev.native.at(kind).cycles);
                const uint64_t calls = Delta(current.native.at(kind).calls, prev.native.at(kind).calls);
                row.hasNative = row.hasNative || calls > 0;
                row.native.percent.at(kind) = Percent(cycles, windowCycles);
                row.native.callsPerSecond.at(kind) = static_cast<double>(calls) / window;
                row.nativePercent += row.native.percent.at(kind);
            }
            // Whatever a JS thread burned outside a binding is JS (or the pump around it). Only
            // meaningful on a thread the native timing has seen: elsewhere it would just be CPU.
            if (row.hasNative) {
                row.jsPercent = std::max(0.0, row.cpuPercent - row.nativePercent);
            }

            Append(state.cpu, row.cpuPercent);
            Append(state.native, row.nativePercent);
            if (row.hasNative) {
                Append(state.js, row.jsPercent);
            }
        }
        state.prev = current;

        row.peakPercent = Peak(state.cpu);
        row.peakNativePercent = Peak(state.native);
        row.peakJsPercent = Peak(state.js);
        row.hasNativePeak = row.hasNative || row.peakNativePercent > 0.0;
        row.hasJsPeak = !state.js.empty();
        row.history = state.cpu;
        next.emplace(tid, std::move(state));

        // Totalled before filtering, so the process figure covers every thread.
        totalCpu += row.cpuPercent;

        // Filtered on the peak: a thread that spiked earlier in the window is what this is for.
        if (row.peakPercent >= INTERESTING_CPU_PERCENT || row.hasLoopCounters || showAllThreads_) {
            row.name = thread_utils::GetThreadDescription(tid);
            rows.push_back(std::move(row));
        }
    }
    threadState_ = std::move(next);
    rows_ = std::move(rows);
    threadCount_ = threads.size();
    totalCpu_ = totalCpu;
    SortThreadRows();
}

void ProfilingPanel::SortThreadRows() {
    const bool up = threadSort_.ascending;
    switch (threadSort_.column) {
        case ThreadColTid:
            SortRows(rows_, up, &Row::tid);
            break;
        case ThreadColCpu:
            SortRows(rows_, up, &Row::cpuPercent);
            break;
        case ThreadColPeak:
            SortRows(rows_, up, &Row::peakPercent);
            break;
        case ThreadColNative:
            SortRows(rows_, up, &Row::nativePercent, &Row::hasNative);
            break;
        case ThreadColJs:
            SortRows(rows_, up, &Row::jsPercent, &Row::hasNative);
            break;
        case ThreadColPeakNative:
            SortRows(rows_, up, &Row::peakNativePercent, &Row::hasNativePeak);
            break;
        case ThreadColPeakJs:
            SortRows(rows_, up, &Row::peakJsPercent, &Row::hasJsPeak);
            break;
        case ThreadColWakes:
            SortRows(rows_, up, &Row::wakesPerSecond, &Row::hasLoopCounters);
            break;
        case ThreadColAwake:
            SortRows(rows_, up, &Row::awakePercent, &Row::hasLoopCounters);
            break;
        case ThreadColName:
            SortRows(rows_, up, &Row::name);
            break;
        default:
            SortRows(rows_, false, &Row::cpuPercent);
            break;
    }
}

void ProfilingPanel::SampleNativeBindings() {
    auto samples = script::SnapshotNativeBindings();

    bindingCycleTotal_ = 0;
    bindingCallTotal_ = 0;
    for (const auto& sample : samples) {
        bindingCycleTotal_ += sample.cycles;
        bindingCallTotal_ += sample.calls;
    }

    // The top by CPU, then the top stallers from the remainder: delay() burns no CPU and would
    // otherwise never appear. Never ranked on the two summed - that is wall time, and delay buries
    // the rest under it.
    std::ranges::sort(samples, std::ranges::greater{}, &script::NativeBindingSample::cycles);
    const auto rest = samples.begin() + static_cast<std::ptrdiff_t>(std::min(samples.size(), TOP_BINDINGS));
    std::ranges::sort(std::ranges::subrange(rest, samples.end()), std::ranges::greater{},
                      &script::NativeBindingSample::blockedCycles);
    auto end = rest;
    for (size_t kept = 0; end != samples.end() && kept < TOP_BLOCKED_BINDINGS && end->blockedCycles > 0; ++kept) {
        ++end;
    }
    samples.erase(end, samples.end());

    bindings_ = std::move(samples);
    SortBindingRows();
}

void ProfilingPanel::SortBindingRows() {
    using Sample = script::NativeBindingSample;
    const bool up = bindingSort_.ascending;
    switch (bindingSort_.column) {
        case BindingColKind:
            SortRows(bindings_, up, [](const Sample& s) { return std::string_view(KindLabel(s.kind)); });
            break;
        case BindingColCalls:
            SortRows(bindings_, up, &Sample::calls);
            break;
        case BindingColPerCall:
            SortRows(bindings_, up, [](const Sample& s) { return PerCall(s.cycles, s.calls); });
            break;
        case BindingColBlocked:
            SortRows(
                bindings_, up, [](const Sample& s) { return PerCall(s.blockedCycles, s.calls); },
                [](const Sample& s) { return s.blockedCycles > 0; });
            break;
        case BindingColName:
            SortRows(bindings_, up, &Sample::name);
            break;
        case BindingColShare:
        case BindingColCycles:
            SortRows(bindings_, up, &Sample::cycles);
            break;
        default:
            SortRows(bindings_, false, &Sample::cycles);
            break;
    }
}

double ProfilingPanel::CollectedSeconds() const {
    auto elapsed = collected_;
    if (collecting_) {
        elapsed += std::chrono::steady_clock::now() - collectStart_;
    }
    return elapsed.count();
}

void ProfilingPanel::DrawNativeCalls() {
    const double collected = CollectedSeconds();
    std::string header = "Native calls";
    if (!bindings_.empty()) {
        header += fmt::format("  {} calls over {}", Grouped(bindingCallTotal_), FormatDuration(collected));
        if (collected > 0.0) {
            header += fmt::format(" ({:.0f}/s)", static_cast<double>(bindingCallTotal_) / collected);
        }
        // The absolute anchor for the share column: a large share of a small total is still small.
        if (collected > 0.0 && tscHz_ > 0.0) {
            header += fmt::format(", {:.2f}% of one core in bindings",
                                  100.0 * static_cast<double>(bindingCycleTotal_) / (collected * tscHz_));
        }
    }
    header += "###native";
    if (!ImGui::CollapsingHeader(header.c_str())) {
        return;
    }

    if (bindings_.empty()) {
        ImGui::TextDisabled(profiling::nativeTimingEnabled.load(std::memory_order_relaxed)
                                ? "Waiting for a JS->native call..."
                                : "Tick 'Time native calls' to attribute native time per binding.");
        return;
    }

    if (ImGui::SmallButton("Reset")) {
        script::ResetNativeBindings();
        bindings_.clear();
        bindingCycleTotal_ = 0;
        bindingCallTotal_ = 0;
        collected_ = {};
        collectStart_ = std::chrono::steady_clock::now();
        return;
    }
    ImGui::SameLine();
    // Cumulative rather than a rate: the expensive binding is often a rare one.
    ImGui::TextDisabled("(share of all native CPU since the last reset)");

    if (!ImGui::BeginTable("##bindings", BindingColCount, SORTABLE_TABLE_FLAGS, ImVec2(0.0F, BINDING_TABLE_HEIGHT))) {
        return;
    }
    ImGui::TableSetupColumn("share", NUMERIC_COLUMN | ImGuiTableColumnFlags_DefaultSort, 60.0F);
    ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, 40.0F);
    ImGui::TableSetupColumn("calls", NUMERIC_COLUMN, 110.0F);
    ImGui::TableSetupColumn("CPU", NUMERIC_COLUMN, 90.0F);
    ImGui::TableSetupColumn("CPU/call", NUMERIC_COLUMN, 90.0F);
    ImGui::TableSetupColumn("blocked/call", NUMERIC_COLUMN, 100.0F);
    ImGui::TableSetupColumn("binding");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    if (TakeSortSpec(bindingSort_.column, bindingSort_.ascending)) {
        SortBindingRows();
    }

    for (const auto& row : bindings_) {
        const double blockedPerCall = PerCall(row.blockedCycles, row.calls);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        TextPercent(Percent(row.cycles, bindingCycleTotal_), 10.0, 30.0);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", KindLabel(row.kind));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Grouped(row.calls).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(FormatCycles(static_cast<double>(row.cycles), tscHz_).c_str());
        ImGui::TableNextColumn();
        // Separates "expensive call" from "cheap call made constantly".
        ImGui::TextUnformatted(FormatCycles(PerCall(row.cycles, row.calls), tscHz_).c_str());
        ImGui::TableNextColumn();
        // Not in the share (the thread parked, not working), but a binding that stalls the script
        // every call is still worth finding.
        OrDash(blockedPerCall > 0.0, [&] { ImGui::TextUnformatted(FormatCycles(blockedPerCall, tscHz_).c_str()); });
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.name.data(), row.name.data() + row.name.size());
    }
    ImGui::EndTable();
}

void ProfilingPanel::DrawThreads() {
    const std::string header = fmt::format("Threads  {:.1f}% of one core across {} threads ({} shown)###threads",
                                           totalCpu_, threadCount_, rows_.size());
    if (!ImGui::CollapsingHeader(header.c_str())) {
        return;
    }

    const float height = std::max(ImGui::GetContentRegionAvail().y, THREAD_TABLE_MIN_HEIGHT);
    if (!ImGui::BeginTable("##threads", ThreadColCount, SORTABLE_TABLE_FLAGS, ImVec2(0.0F, height))) {
        return;
    }
    ImGui::TableSetupColumn("tid", ImGuiTableColumnFlags_WidthFixed, 60.0F);
    ImGui::TableSetupColumn("CPU", NUMERIC_COLUMN | ImGuiTableColumnFlags_DefaultSort, 60.0F);
    ImGui::TableSetupColumn("peak", NUMERIC_COLUMN, 60.0F);
    ImGui::TableSetupColumn("native", NUMERIC_COLUMN, 60.0F);
    ImGui::TableSetupColumn("JS", NUMERIC_COLUMN, 60.0F);
    ImGui::TableSetupColumn("peak nat.", NUMERIC_COLUMN, 70.0F);
    ImGui::TableSetupColumn("peak JS", NUMERIC_COLUMN, 70.0F);
    ImGui::TableSetupColumn("wakes/s", NUMERIC_COLUMN, 70.0F);
    ImGui::TableSetupColumn("awake", NUMERIC_COLUMN, 60.0F);
    ImGui::TableSetupColumn("history", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, HISTORY_WIDTH);
    ImGui::TableSetupColumn("thread");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    if (TakeSortSpec(threadSort_.column, threadSort_.ascending)) {
        SortThreadRows();
    }

    float scale = HISTORY_SCALE_FLOOR;
    for (const auto& row : rows_) {
        scale = std::max(scale, static_cast<float>(row.peakPercent));
    }

    for (const auto& row : rows_) {
        // The "##" widgets below draw no label, so the row needs an ID of its own.
        ImGui::PushID(static_cast<int32_t>(row.tid));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", row.tid);
        ImGui::TableNextColumn();
        TextPercent(row.cpuPercent, 5.0, 15.0);
        ImGui::TableNextColumn();
        TextPercent(row.peakPercent, 5.0, 15.0);
        ImGui::TableNextColumn();
        OrDash(row.hasNative, [&] {
            TextPercent(row.nativePercent, 5.0, 15.0);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("functions  %6.2f%%  (%.0f/s)\n"
                                  "getters    %6.2f%%  (%.0f/s)\n"
                                  "setters    %6.2f%%  (%.0f/s)",
                                  row.native.percent[0], row.native.callsPerSecond[0], row.native.percent[1],
                                  row.native.callsPerSecond[1], row.native.percent[2], row.native.callsPerSecond[2]);
            }
        });
        ImGui::TableNextColumn();
        OrDash(row.hasNative, [&] {
            TextPercent(row.jsPercent, 5.0, 15.0);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("CPU outside bindings: the script's own JS, plus the event pump around it.");
            }
        });
        ImGui::TableNextColumn();
        OrDash(row.hasNativePeak, [&] { TextPercent(row.peakNativePercent, 5.0, 15.0); });
        ImGui::TableNextColumn();
        OrDash(row.hasJsPeak, [&] { TextPercent(row.peakJsPercent, 5.0, 15.0); });
        ImGui::TableNextColumn();
        OrDash(row.hasLoopCounters, [&] { ImGui::Text("%.0f", row.wakesPerSecond); });
        ImGui::TableNextColumn();
        // High with low CPU means many short wakeups; high with high CPU means the loop body itself
        // is expensive.
        OrDash(row.hasLoopCounters, [&] { TextPercent(row.awakePercent, 10.0, 30.0); });
        ImGui::TableNextColumn();
        OrDash(row.history.size() > 1, [&] {
            ImGui::PlotLines("##history", row.history.data(), static_cast<int32_t>(row.history.size()), 0, nullptr,
                             0.0F, scale, ImVec2(HISTORY_WIDTH, HISTORY_HEIGHT));
            if (ImGui::IsItemHovered()) {
                const double span = static_cast<double>(row.history.size()) * SAMPLE_SECONDS;
                ImGui::SetTooltip("CPU%% over the last %.0fs, plotted 0-%.0f%%", span, static_cast<double>(scale));
            }
        });
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.name.empty() ? "(unnamed)" : row.name.c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

}  // namespace d2bs::js::console

#endif  // D2BS_PROFILING
