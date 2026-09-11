#include "Profiling.h"

#include <Windows.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "threadutils.h"

#ifdef D2BS_PROFILING

namespace d2bs::profiling {

uint64_t Cycles() {
    return __rdtsc();
}

namespace {

// Items are shared_ptr so a snapshot in progress cannot race an owner going away: the registry
// drops its reference, the reader keeps the item alive until done.
template <typename T>
class Registry {
   public:
    void Add(std::shared_ptr<T> item) {
        const std::scoped_lock lock(mutex_);
        items_.push_back(std::move(item));
    }

    void Remove(const std::shared_ptr<T>& item) {
        const std::scoped_lock lock(mutex_);
        std::erase(items_, item);
    }

    [[nodiscard]] std::vector<std::shared_ptr<T>> Live() {
        const std::scoped_lock lock(mutex_);
        return items_;
    }

   private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<T>> items_;
};

struct ThreadEntry {
    uint32_t tid = 0;
    ThreadCounters counters;
};

Registry<ThreadEntry>& Threads() {
    static Registry<ThreadEntry> registry;
    return registry;
}

// Held in a thread_local, so its destructor unregisters the thread on exit.
class Registration {
   public:
    Registration() : entry_(std::make_shared<ThreadEntry>()) {
        entry_->tid = GetCurrentThreadId();
        Threads().Add(entry_);
    }

    ~Registration() { Threads().Remove(entry_); }

    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;
    Registration(Registration&&) = delete;
    Registration& operator=(Registration&&) = delete;

    [[nodiscard]] ThreadCounters& Counters() const { return entry_->counters; }
    [[nodiscard]] std::shared_ptr<ThreadCounters> Shared() const { return {entry_, &entry_->counters}; }

   private:
    std::shared_ptr<ThreadEntry> entry_;
};

// A foreign thread without backed TLS (see thread_utils::HasThreadLocalStorage) would
// access-violate on a thread_local; such a thread never runs one of the measured loops, so it gets
// a shared throw-away block.
ThreadCounters& Orphan() {
    static ThreadCounters orphan;
    return orphan;
}

Registration& CurrentRegistration() {
    static thread_local Registration registration;
    return registration;
}

}  // namespace

ThreadCounters& Current() {
    if (!thread_utils::HasThreadLocalStorage()) {
        return Orphan();
    }
    return CurrentRegistration().Counters();
}

std::shared_ptr<ThreadCounters> CurrentShared() {
    if (!thread_utils::HasThreadLocalStorage()) {
        return {std::shared_ptr<void>{}, &Orphan()};
    }
    return CurrentRegistration().Shared();
}

std::vector<ThreadSample> SnapshotThreads() {
    std::unordered_map<uint32_t, std::shared_ptr<ThreadEntry>> registered;
    for (auto& entry : Threads().Live()) {
        registered.emplace(entry->tid, std::move(entry));
    }

    std::vector<ThreadSample> samples;
    thread_utils::ForEachProcessThread([&](HANDLE handle, uint32_t tid) {
        ULONG64 cpuCycles = 0;
        if (QueryThreadCycleTime(handle, &cpuCycles) == 0) {
            return;
        }
        ThreadSample sample{.tid = tid, .cpuCycles = cpuCycles};
        if (const auto it = registered.find(tid); it != registered.end()) {
            const ThreadCounters& counters = it->second->counters;
            sample.wakeups = counters.wakeups.load(std::memory_order_relaxed);
            sample.sleptCycles = counters.sleptCycles.load(std::memory_order_relaxed);
            sample.workCycles = counters.workCycles.load(std::memory_order_relaxed);
            for (size_t kind = 0; kind < NATIVE_CALL_KINDS; ++kind) {
                sample.native.at(kind) = {.cycles = counters.native.at(kind).cycles.load(std::memory_order_relaxed),
                                          .calls = counters.native.at(kind).calls.load(std::memory_order_relaxed)};
            }
        }
        samples.push_back(sample);
    });
    std::ranges::sort(samples, {}, &ThreadSample::tid);
    return samples;
}

// ============================================================================
// Timeline
// ============================================================================

struct Timeline::Record {
    struct PhaseCounter {
        std::atomic<uint64_t> cycles{0};
        std::atomic<uint64_t> entries{0};
    };

    explicit Record(const TimelineInfo& timeline) : info(&timeline), counters(timeline.phases.size()) {}

    const TimelineInfo* info;
    std::vector<PhaseCounter> counters;  // sized once; atomics cannot move
    std::mutex detailMutex;
    std::string detail;
};

namespace {

Registry<Timeline::Record>& Timelines() {
    static Registry<Timeline::Record> registry;
    return registry;
}

// Totals of destroyed timelines, per title, so a script restart does not make the merged figure
// go backwards.
struct Retired {
    std::mutex mutex;
    std::vector<TimelineSample> totals;
};

Retired& RetiredTimelines() {
    static Retired retired;
    return retired;
}

TimelineSample& FindOrAdd(std::vector<TimelineSample>& samples, const TimelineInfo& info) {
    const std::string_view title = info.title;
    auto it = std::ranges::find_if(samples, [title](const TimelineSample& s) { return s.info->title == title; });
    if (it == samples.end()) {
        it = samples.insert(samples.end(), TimelineSample{.info = &info});
        it->phases.resize(info.phases.size());
    }
    return *it;
}

void AddCounters(TimelineSample& sample, const Timeline::Record& record) {
    for (size_t i = 0; i < record.counters.size() && i < sample.phases.size(); ++i) {
        sample.phases[i].cycles += record.counters[i].cycles.load(std::memory_order_relaxed);
        sample.phases[i].entries += record.counters[i].entries.load(std::memory_order_relaxed);
    }
}

}  // namespace

Timeline::Timeline(const TimelineInfo& info) : phases_(info.phases), record_(std::make_shared<Record>(info)) {
    Timelines().Add(record_);
}

Timeline::~Timeline() {
    Leave();
    Timelines().Remove(record_);
    auto& retired = RetiredTimelines();
    const std::scoped_lock lock(retired.mutex);
    AddCounters(FindOrAdd(retired.totals, *record_->info), *record_);
}

void Timeline::Enter(size_t phase) {
    const uint64_t now = Cycles();
    // Bound lazily: a timeline held by a singleton is built on whichever thread first touched it.
    if (thread_ == nullptr) {
        thread_ = CurrentShared();
    }
    const bool wasParked = current_ != NONE && phases_[current_].blocking;
    Close(now);

    current_ = phase;
    start_ = now;
    sleptAtStart_ = thread_->sleptCycles.load(std::memory_order_relaxed);
    if (wasParked && !phases_[phase].blocking) {
        thread_->Wake();
    }
    record_->counters[phase].entries.fetch_add(1, std::memory_order_relaxed);
}

void Timeline::Leave() {
    if (current_ != NONE) {
        Close(Cycles());
    }
}

void Timeline::Resume(size_t phase) {
    const uint64_t now = Cycles();
    Close(now);
    if (phase == NONE) {
        return;
    }
    current_ = phase;
    start_ = now;
    sleptAtStart_ = thread_->sleptCycles.load(std::memory_order_relaxed);
}

// Unbound counts as "not this thread": binding is left to the loop itself, so a hook that fires
// before the loop's first Enter cannot bind the timeline to the wrong thread.
bool Timeline::OnThisThread() const {
    return thread_ != nullptr && thread_.get() == &Current();
}

void Timeline::Close(uint64_t now) {
    if (current_ == NONE) {
        return;
    }
    const uint64_t elapsed = now - start_;
    record_->counters[current_].cycles.fetch_add(elapsed, std::memory_order_relaxed);

    // Waits taken inside the phase (a ScopedSleep on the game lock, say) already went to slept.
    const uint64_t nested = thread_->sleptCycles.load(std::memory_order_relaxed) - sleptAtStart_;
    const uint64_t own = elapsed > nested ? elapsed - nested : 0;
    if (phases_[current_].blocking) {
        thread_->AddSlept(own);
    } else {
        thread_->AddWork(own);
    }
    current_ = NONE;
}

void Timeline::SetDetail(std::string detail) {
    const std::scoped_lock lock(record_->detailMutex);
    record_->detail = std::move(detail);
}

Timeline::Scope::Scope(Timeline& timeline, size_t phase) {
    if (!timeline.OnThisThread() || timeline.current_ == NONE) {
        return;
    }
    timeline_ = &timeline;
    outer_ = timeline.current_;
    timeline.Enter(phase);
}

Timeline::Scope::~Scope() {
    if (timeline_ != nullptr) {
        timeline_->Resume(outer_);
    }
}

std::vector<TimelineSample> SnapshotTimelines() {
    std::vector<TimelineSample> samples;
    {
        auto& retired = RetiredTimelines();
        const std::scoped_lock lock(retired.mutex);
        samples = retired.totals;
    }
    for (const auto& record : Timelines().Live()) {
        TimelineSample& merged = FindOrAdd(samples, *record->info);
        if (merged.detail.empty()) {
            const std::scoped_lock lock(record->detailMutex);
            merged.detail = record->detail;
        }
        AddCounters(merged, *record);
    }
    return samples;
}

}  // namespace d2bs::profiling

#endif  // D2BS_PROFILING
