#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Lightweight counters answering "where does the CPU go", read by the console's Profiling panel.
//
// Everything is timed with __rdtsc(): the speedhack detours QueryPerformanceCounter (and the other
// wall clocks scripts see) on script threads and the game thread, so std::chrono reports virtual
// time there. Only ratios are computed from the cycle counts, so the TSC frequency never has to be
// known, and per-thread CPU (QueryThreadCycleTime) comes in the same unit.
//
// The recording paths - Timeline::Enter, ScopedSleep, ScopedNativeCall - take no locks; the
// registries are locked only on thread / loop registration and on the panel's snapshots.
//
// Without D2BS_PROFILING (MSBuild D2bsProfiling=false) the recording types below are empty inline
// stubs with the same surface, so call sites compile unchanged and the optimizer drops them.

namespace d2bs::profiling {

// Which trampoline a JS->native call arrived through. Kept apart because a script paying per
// property access and one paying inside a few calls need different fixes.
enum class NativeCall : uint8_t { Function, Getter, Setter };
constexpr size_t NATIVE_CALL_KINDS = 3;

// One segment of a loop, as the Profiling panel presents it. The loop itself only names phases.
struct PhaseInfo {
    const char* name = "";
    const char* what = "";
    // The thread is parked for the whole phase: counted as slept, and leaving it is a wakeup.
    bool blocking = false;
    // Not this framework's cost (the game's own frame). In the denominator, but not in "ours".
    bool foreign = false;
    // Share thresholds that colour the row; zero leaves it plain.
    double warn = 0.0;
    double bad = 0.0;
};

// A registered timeline's presentation. Must outlive the timeline - a constexpr next to the loop.
struct TimelineInfo {
    const char* title = "";
    std::span<const PhaseInfo> phases;
    size_t framePhase = 0;  // entries into this phase are the loop's iterations
    const char* frameLabel = "frames";
    const char* idle = "No frames in this window.";
    // Position among the panel's sections: below 100 draws before the panel's own native-calls and
    // threads sections, 100 or more after them.
    int32_t order = 0;
    bool expanded = false;  // open on the panel's first draw
};

#ifdef D2BS_PROFILING

// Out-of-line: __rdtsc is declared by both <intrin.h> and winnt.h, and clang-tidy flags the
// second declaration at a line no NOLINT of ours can reach. LTO inlines it back.
[[nodiscard]] uint64_t Cycles();

// Cumulative cost of one binding, or of one call kind on one thread.
struct NativeStats {
    std::atomic<uint64_t> cycles{0};  // CPU
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> blockedCycles{0};  // asleep in delay(), waiting on the game thread, ...
};

// One block per thread. Relaxed atomics throughout - a torn read costs one stale sample.
struct ThreadCounters {
    std::atomic<uint64_t> wakeups{0};      // idle-loop passes
    std::atomic<uint64_t> sleptCycles{0};  // inside a timed wait
    std::atomic<uint64_t> workCycles{0};   // awake in the loop

    // CPU (not elapsed) SELF time inside JS->native calls, per kind: the three sum to the thread's
    // native time and the remainder of its CPU is JS. Blocked spans are excluded via sleptCycles.
    std::array<NativeStats, NATIVE_CALL_KINDS> native;

    // Nesting state for the self-time arithmetic. Plain members: owning thread only.
    uint64_t nativeChildCycles = 0;
    uint64_t nativeChildBlocked = 0;
    int32_t nativeDepth = 0;

    void AddSlept(uint64_t cycles) { sleptCycles.fetch_add(cycles, std::memory_order_relaxed); }
    void AddWork(uint64_t cycles) { workCycles.fetch_add(cycles, std::memory_order_relaxed); }
    void Wake() { wakeups.fetch_add(1, std::memory_order_relaxed); }
    NativeStats& Native(NativeCall kind) { return native.at(static_cast<size_t>(kind)); }
};

// The calling thread's block, registered on first use and dropped when the thread exits. The
// shared form keeps the block alive for a holder that may outlive the thread (a singleton's
// Timeline at process exit).
[[nodiscard]] ThreadCounters& Current();
[[nodiscard]] std::shared_ptr<ThreadCounters> CurrentShared();

// ============================================================================
// Timeline - a phase stopwatch for a loop
// ============================================================================

// Attributes a loop's elapsed time to whichever phase it is in. Enter(phase) charges the time since
// the previous Enter to the previous phase, so a loop is instrumented with one call per boundary;
// `break` / `continue` need nothing because the next Enter (or the destructor) closes what is open.
// Feeds the calling thread's counters (work / slept / wakeups, from the blocking flags) and the
// per-phase totals the panel lists. Single-thread use only.
class Timeline {
   public:
    explicit Timeline(const TimelineInfo& info);
    ~Timeline();

    template <typename Phase>
    void Enter(Phase phase) {
        Enter(static_cast<size_t>(phase));
    }
    void Enter(size_t phase);

    // Closes the current phase without opening another.
    void Leave();

    // Re-opens a phase without counting an entry, for a loop that ran nested inside it.
    template <typename Phase>
    void Resume(Phase phase) {
        Resume(static_cast<size_t>(phase));
    }
    void Resume(size_t phase);

    [[nodiscard]] bool InPhase() const { return current_ != NONE; }

    // A phase nested in the current one: entered on construction, with the outer phase resumed on
    // destruction (not counted as a new entry). Inert when no phase is open or on any thread but
    // the timeline's, so a hook that can fire anywhere uses it unconditionally.
    class Scope {
       public:
        Scope(Timeline& timeline, size_t phase);
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

       private:
        Timeline* timeline_ = nullptr;  // null when inert
        size_t outer_ = NONE;
    };

    template <typename Phase>
    [[nodiscard]] Scope Nest(Phase phase) {
        return {*this, static_cast<size_t>(phase)};
    }

    // A line the panel shows under the title, e.g. which renderer the console landed on.
    void SetDetail(std::string detail);

    Timeline(const Timeline&) = delete;
    Timeline& operator=(const Timeline&) = delete;
    Timeline(Timeline&&) = delete;
    Timeline& operator=(Timeline&&) = delete;

    struct Record;

   private:
    static constexpr size_t NONE = SIZE_MAX;

    void Close(uint64_t now);
    [[nodiscard]] bool OnThisThread() const;

    std::span<const PhaseInfo> phases_;
    std::shared_ptr<Record> record_;
    std::shared_ptr<ThreadCounters> thread_;
    size_t current_ = NONE;
    uint64_t start_ = 0;
    uint64_t sleptAtStart_ = 0;
};

struct PhaseSample {
    uint64_t cycles = 0;
    uint64_t entries = 0;
};

// One title's cumulative totals, indexed like `info->phases`. Timelines sharing a title (one per
// script thread, say) are summed, and a destroyed timeline's totals stay in the sum.
struct TimelineSample {
    const TimelineInfo* info = nullptr;
    std::string detail;
    std::vector<PhaseSample> phases;
};

// Every registered title, in registration order.
[[nodiscard]] std::vector<TimelineSample> SnapshotTimelines();

// ============================================================================
// Native call timing
// ============================================================================

// Off by default: property getters go through the same trampoline, so on a script reading me.x in
// a loop this is the hottest path in the process and two rdtsc reads can exceed the callback.
inline std::atomic nativeTimingEnabled{false};

// Charges its lifetime to a category on the calling thread and to one binding's stats. SELF time:
// a binding that re-enters JS which calls another binding charges the inner span to the inner one.
class ScopedNativeCall {
   public:
    ScopedNativeCall(NativeCall kind, NativeStats& stats) : stats_(stats), kind_(kind) {
        if (!nativeTimingEnabled.load(std::memory_order_relaxed)) {
            return;
        }
        counters_ = &Current();
        counters_->Native(kind).calls.fetch_add(1, std::memory_order_relaxed);
        stats_.calls.fetch_add(1, std::memory_order_relaxed);
        parentChildCycles_ = counters_->nativeChildCycles;
        parentChildBlocked_ = counters_->nativeChildBlocked;
        counters_->nativeChildCycles = 0;
        counters_->nativeChildBlocked = 0;
        ++counters_->nativeDepth;
        startSlept_ = counters_->sleptCycles.load(std::memory_order_relaxed);
        start_ = Cycles();
    }

    ~ScopedNativeCall() {
        if (counters_ == nullptr) {
            return;
        }
        const uint64_t elapsed = Cycles() - start_;

        // sleptCycles is cumulative, so its growth covers our own waits and our children's.
        const uint64_t slept = counters_->sleptCycles.load(std::memory_order_relaxed) - startSlept_;
        const uint64_t blocked = elapsed > slept ? slept : elapsed;
        const uint64_t cpu = elapsed - blocked;

        const uint64_t childCpu = counters_->nativeChildCycles;
        const uint64_t childBlocked = counters_->nativeChildBlocked;
        const uint64_t self = cpu > childCpu ? cpu - childCpu : 0;
        const uint64_t selfBlocked = blocked > childBlocked ? blocked - childBlocked : 0;

        counters_->Native(kind_).cycles.fetch_add(self, std::memory_order_relaxed);
        stats_.cycles.fetch_add(self, std::memory_order_relaxed);
        // Skipped when zero: a 64-bit RMW is a cmpxchg8b loop on Win32, and most calls never block.
        if (selfBlocked != 0) {
            stats_.blockedCycles.fetch_add(selfBlocked, std::memory_order_relaxed);
        }

        // Hand this call's INCLUSIVE figures up to the caller; the outermost call has none.
        --counters_->nativeDepth;
        const bool nested = counters_->nativeDepth != 0;
        counters_->nativeChildCycles = nested ? parentChildCycles_ + cpu : 0;
        counters_->nativeChildBlocked = nested ? parentChildBlocked_ + blocked : 0;
    }

    ScopedNativeCall(const ScopedNativeCall&) = delete;
    ScopedNativeCall& operator=(const ScopedNativeCall&) = delete;
    ScopedNativeCall(ScopedNativeCall&&) = delete;
    ScopedNativeCall& operator=(ScopedNativeCall&&) = delete;

   private:
    ThreadCounters* counters_ = nullptr;
    NativeStats& stats_;
    uint64_t start_ = 0;
    uint64_t startSlept_ = 0;
    uint64_t parentChildCycles_ = 0;
    uint64_t parentChildBlocked_ = 0;
    NativeCall kind_;
};

// A span inside a native call that is not the call's own work - JS run on its behalf, such as the
// event handlers delay() pumps. Its CPU is handed to the enclosing call as child time, like a nested
// binding's, so it lands in "JS / other" rather than on the binding. Blocked time is left to the
// bindings that actually waited. Inert outside a timed native call.
class ScopedNativeExclusion {
   public:
    ScopedNativeExclusion() {
        if (!nativeTimingEnabled.load(std::memory_order_relaxed)) {
            return;
        }
        ThreadCounters& counters = Current();
        if (counters.nativeDepth == 0) {
            return;
        }
        counters_ = &counters;
        childCyclesAtStart_ = counters.nativeChildCycles;
        startSlept_ = counters.sleptCycles.load(std::memory_order_relaxed);
        start_ = Cycles();
    }

    ~ScopedNativeExclusion() {
        if (counters_ == nullptr) {
            return;
        }
        const uint64_t elapsed = Cycles() - start_;
        const uint64_t slept = counters_->sleptCycles.load(std::memory_order_relaxed) - startSlept_;
        const uint64_t cpu = elapsed > slept ? elapsed - slept : 0;
        // Replaces, not adds: bindings called inside the span already handed up their inclusive
        // time, and the span's own figure covers them.
        counters_->nativeChildCycles = childCyclesAtStart_ + cpu;
    }

    ScopedNativeExclusion(const ScopedNativeExclusion&) = delete;
    ScopedNativeExclusion& operator=(const ScopedNativeExclusion&) = delete;
    ScopedNativeExclusion(ScopedNativeExclusion&&) = delete;
    ScopedNativeExclusion& operator=(ScopedNativeExclusion&&) = delete;

   private:
    ThreadCounters* counters_ = nullptr;
    uint64_t start_ = 0;
    uint64_t startSlept_ = 0;
    uint64_t childCyclesAtStart_ = 0;
};

struct NativeKindSample {
    uint64_t cycles = 0;
    uint64_t calls = 0;
};

struct ThreadSample {
    uint32_t tid = 0;
    // The kernel's count of CPU cycles the thread has run (QueryThreadCycleTime: TSC deltas
    // accumulated at every context switch, so short bursts count exactly, unlike the tick-sampled
    // GetThreadTimes). Same unit as Cycles().
    uint64_t cpuCycles = 0;
    // The counters below are zero for a thread that never touched Current().
    uint64_t wakeups = 0;
    uint64_t sleptCycles = 0;
    uint64_t workCycles = 0;
    std::array<NativeKindSample, NATIVE_CALL_KINDS> native{};
};

// Every thread of the process, ordered by tid.
[[nodiscard]] std::vector<ThreadSample> SnapshotThreads();

// A wait INSIDE some other span - a lock acquisition, a poll for game readiness, a task posted to
// the game thread - so the enclosing native call or timeline phase can separate blocked from busy.
// A loop's own sleep is a blocking phase on its Timeline instead.
class ScopedSleep {
   public:
    ScopedSleep() : start_(Cycles()) {}
    ~ScopedSleep() { Current().AddSlept(Cycles() - start_); }

    ScopedSleep(const ScopedSleep&) = delete;
    ScopedSleep& operator=(const ScopedSleep&) = delete;
    ScopedSleep(ScopedSleep&&) = delete;
    ScopedSleep& operator=(ScopedSleep&&) = delete;

   private:
    uint64_t start_;
};

#else  // D2BS_PROFILING

// The constructors are user-provided so a `const` local of these types is well-formed and is not
// reported as unused.
// NOLINTBEGIN(modernize-use-equals-default,hicpp-use-equals-default)

struct NativeStats {};

class Timeline {
   public:
    class Scope {
       public:
        Scope() {}
    };

    explicit Timeline(const TimelineInfo& /*info*/) {}

    template <typename Phase>
    void Enter(Phase /*phase*/) {}
    void Leave() {}
    template <typename Phase>
    void Resume(Phase /*phase*/) {}
    [[nodiscard]] bool InPhase() const { return false; }
    template <typename Phase>
    [[nodiscard]] Scope Nest(Phase /*phase*/) {
        return {};
    }
    void SetDetail(const std::string& /*detail*/) {}
};

class ScopedNativeCall {
   public:
    ScopedNativeCall(NativeCall /*kind*/, NativeStats& /*stats*/) {}
};

class ScopedNativeExclusion {
   public:
    ScopedNativeExclusion() {}
};

class ScopedSleep {
   public:
    ScopedSleep() {}
};

// NOLINTEND(modernize-use-equals-default,hicpp-use-equals-default)

#endif  // D2BS_PROFILING

}  // namespace d2bs::profiling
