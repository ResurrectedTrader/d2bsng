#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "components/console/Panel.h"
#include "components/script/NativeCallHook.h"
#include "utils/Profiling.h"

#ifdef D2BS_PROFILING

namespace d2bs::js::console {

// Where the process's CPU goes: per registered loop (game thread, console), per native binding,
// and per thread. Exists to answer whether this framework burns more CPU than legacy d2bs, which is
// a question about idle behaviour - how often each thread wakes and how much it does per wake.
//
// Everything is TSC cycles over the same window - per-thread CPU (QueryThreadCycleTime), time in
// bindings, and the window itself - so the shares are plain ratios. Rates are measured over fixed
// windows and redrawn unchanged in between: a per-draw window is shorter than one game frame, so
// the figures would blink.
class ProfilingPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Profiling"; }
    void Draw() override;

   private:
    // One registered loop over the last window. Rows come from the loop's own phase table.
    struct TimelineView {
        const profiling::TimelineInfo* info = nullptr;
        std::string detail;
        double framesPerSecond = 0.0;
        std::vector<uint64_t> cycles;  // per phase
        // This framework's non-blocking phases as a share of the loop; only shown against a
        // foreign phase, where it is the answer to "how much of the game thread is ours".
        double oursPercent = 0.0;
        double peakOursPercent = 0.0;  // over the history horizon
        bool hasForeign = false;
    };

    // Time inside JS->native calls as a share of one core, per NativeCall kind. Only populated
    // while native timing is armed.
    struct NativeSplit {
        std::array<double, profiling::NATIVE_CALL_KINDS> percent{};
        std::array<double, profiling::NATIVE_CALL_KINDS> callsPerSecond{};
    };

    // One displayed thread. Every column is a plain field, so sorting and the "dash or value" rules
    // are member pointers; the `has*` flags tell "not measured" from "measured, near zero".
    struct Row {
        uint32_t tid = 0;
        std::string name;
        double cpuPercent = 0.0;
        double peakPercent = 0.0;
        double nativePercent = 0.0;  // sum of the split below
        double jsPercent = 0.0;      // CPU outside bindings
        double peakNativePercent = 0.0;
        double peakJsPercent = 0.0;
        double wakesPerSecond = 0.0;
        double awakePercent = 0.0;
        bool hasNative = false;      // native timing was armed and this thread crossed into native
        bool hasNativePeak = false;  // has a native trace worth showing
        bool hasJsPeak = false;
        bool hasLoopCounters = false;
        NativeSplit native;
        std::vector<float> history;  // a copy: the per-thread state is rebuilt each window
    };

    // Held across windows, since the rows are rebuilt every sample.
    struct SortSpec {
        int32_t column = -1;  // -1 = the table's own default
        bool ascending = false;
    };

    // Per-thread state carried across windows: the cumulative counters at the last boundary and
    // the CPU% / native% / JS% traces over the horizon. Kept for every thread, not just the
    // displayed ones, so a thread has a trace by the time it first spikes.
    struct ThreadState {
        profiling::ThreadSample prev;
        std::vector<float> cpu;
        std::vector<float> native;
        std::vector<float> js;
    };

    struct TimelineState {
        std::vector<profiling::PhaseSample> prev;
        std::vector<float> ours;
    };

    void SampleTimelines(double window);
    void SampleThreads(double window, uint64_t windowCycles);
    void SampleNativeBindings();
    void SortThreadRows();
    void SortBindingRows();
    static void DrawTimeline(const TimelineView& view);
    void DrawNativeCalls();
    void DrawThreads();
    [[nodiscard]] double CollectedSeconds() const;

    std::unordered_map<std::string, TimelineState> timelineState_;
    std::unordered_map<uint32_t, ThreadState> threadState_;
    std::chrono::steady_clock::time_point lastDraw_;
    uint64_t prevSampleCycles_ = 0;
    double sinceSample_ = 0.0;
    bool haveSample_ = false;
    // TSC ticks per second, measured against the wall clock each window. Only for turning cycle
    // counts into readable times; every share is still a ratio of cycles.
    double tscHz_ = 0.0;

    // The last completed window, display-ready.
    std::vector<TimelineView> timelines_;
    std::vector<Row> rows_;
    size_t threadCount_ = 0;  // every enumerated thread, not just the displayed rows
    double totalCpu_ = 0.0;
    std::vector<script::NativeBindingSample> bindings_;  // top by CPU, then the top stallers
    uint64_t bindingCycleTotal_ = 0;
    uint64_t bindingCallTotal_ = 0;

    bool showAllThreads_ = false;

    // How long the binding counters have been accumulating; frozen while timing is off. The
    // counters run whether or not this panel is open, so this is wall time, not sampled windows.
    std::chrono::duration<double> collected_{0.0};
    std::chrono::steady_clock::time_point collectStart_;
    bool collecting_ = false;

    SortSpec threadSort_;
    SortSpec bindingSort_;
};

}  // namespace d2bs::js::console

#endif  // D2BS_PROFILING
