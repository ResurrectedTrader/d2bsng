#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace d2bs::api {

// Transparent comparator map type - allows heterogeneous find/contains with string_view.
using ClassCountMap = std::map<std::string, int32_t, std::less<>>;

// Central registry for per-thread, per-class V8 instance counts.
// Tracks how many live V8-wrapped native objects exist for each class type on each script thread.
// Used for debugging, leak detection, and polling for count==0 during isolate teardown.
//
// Counting sits on every wrapper object's construction and destruction, so the hot path is a
// relaxed increment on a per-thread row with no lock and no lookup.
class V8InstanceTracker {
   public:
    // A fixed-size array of atomics is never restructured, which is what lets a reader and the
    // owning thread touch the same row concurrently.
    static constexpr size_t MAX_CLASSES = 64;

    // Per-thread counts. Opaque to callers, which only hold the Row that Increment handed them
    // and pass it back to Decrement - so a count is always returned to the row that took it, even
    // when the destructor runs on another thread (a V8 weak callback drained by whichever thread
    // disposes the isolate). Rows are immortal, so the pointer stays valid for the lifetime of
    // whatever it counts: Registration never unregisters and ClearThread zeroes instead of
    // erasing, both for reasons of their own.
    struct Row {
        std::thread::id owner;
        std::array<std::atomic<int32_t>, MAX_CLASSES> counts{};
    };

   private:
    struct Registry {
        std::mutex mutex;
        std::vector<std::shared_ptr<Row>> rows;
        // Class name -> row index, assigned on first use of each class and never reused.
        std::vector<std::string> classNames;
    };

    static Registry& GetRegistry() {
        static Registry registry;
        return registry;
    }

    // Registers on construction, and deliberately does not unregister on thread exit: the isolate
    // deleter's leak check can run on another thread after the script thread is gone, and would
    // find nothing. ClearThread is the reclamation path.
    class Registration {
       public:
        Registration() : row_(std::make_shared<Row>()) {
            row_->owner = std::this_thread::get_id();
            auto& registry = GetRegistry();
            const std::scoped_lock lock(registry.mutex);
            registry.rows.push_back(row_);
        }

        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&&) = delete;
        Registration& operator=(Registration&&) = delete;

        [[nodiscard]] Row& Get() const { return *row_; }

       private:
        std::shared_ptr<Row> row_;
    };

    // This thread's row. The Row is heap-allocated and co-owned by the registry - only the handle
    // is thread_local, and only so the writer can find its own row without the lock. Snapshot()
    // reaches the same Rows through the registry, which is how another thread reads them.
    static Row& RowForCurrentThread() {
        static thread_local Registration registration;
        return registration.Get();
    }

   public:
    static V8InstanceTracker& Instance() {
        static V8InstanceTracker tracker;
        return tracker;
    }

    // Row index for a class name. Takes the lock, so callers cache the result - V8ClassBase does so
    // in a static, making it once per class rather than once per object.
    static int32_t ClassId(std::string_view className) {
        auto& registry = GetRegistry();
        const std::scoped_lock lock(registry.mutex);
        for (size_t i = 0; i < registry.classNames.size(); ++i) {
            if (registry.classNames[i] == className) {
                return static_cast<int32_t>(i);
            }
        }
        if (registry.classNames.size() >= MAX_CLASSES) {
            // Uncounted rather than out of bounds, but say so: the failure is a leak check that
            // reports clean for a class that is leaking, which is silent in the wrong direction.
            spdlog::error("V8InstanceTracker: more than {} classes, '{}' will not be counted", MAX_CLASSES, className);
            return -1;
        }
        registry.classNames.emplace_back(className);
        return static_cast<int32_t>(registry.classNames.size() - 1);
    }

    // Counts one instance against the calling thread and returns the row it landed in. The caller
    // must keep that row and hand it to Decrement - which is why the row is returned rather than
    // looked up again on the way out.
    [[nodiscard]] Row& Increment(int32_t classId) {
        Row& row = RowForCurrentThread();
        if (classId >= 0) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by ClassId
            row.counts[static_cast<size_t>(classId)].fetch_add(1, std::memory_order_relaxed);
        }
        return row;
    }

    void Decrement(Row& row, int32_t classId) {
        if (classId >= 0) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by ClassId
            row.counts[static_cast<size_t>(classId)].fetch_sub(1, std::memory_order_relaxed);
        }
    }

    // Returns a snapshot of per-class counts (only non-zero entries).
    // If threadId is provided, returns counts for that thread only; otherwise sums across all threads.
    ClassCountMap Snapshot(std::optional<std::thread::id> threadId = std::nullopt) const {
        // Filtered under the lock rather than copied wholesale: this runs per script per frame.
        std::vector<std::shared_ptr<Row>> rows;
        std::vector<std::string> names;
        {
            auto& registry = GetRegistry();
            const std::scoped_lock lock(registry.mutex);
            names = registry.classNames;
            for (const auto& row : registry.rows) {
                if (!threadId || row->owner == *threadId) {
                    rows.push_back(row);
                }
            }
        }

        // Read outside the lock; owners keep incrementing, which is fine for a diagnostic.
        ClassCountMap merged;
        for (const auto& row : rows) {
            for (size_t i = 0; i < names.size(); ++i) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - i < names.size() <= MAX_CLASSES
                const int32_t count = row->counts[i].load(std::memory_order_relaxed);
                if (count != 0) {
                    merged[names[i]] += count;
                }
            }
        }
        return merged;
    }

    // Reset the counts for a thread (call during isolate teardown after logging any leaks).
    //
    // Zeroes rather than removing the row: Registration owns the row's lifetime, so dropping it
    // here would only orphan it. Thread ids are recycled, so a teardown running late can name a
    // live successor - which would then count into a row no longer reachable from the registry,
    // invisible to Snapshot for the rest of its life and with no path back.
    void ClearThread(std::thread::id threadId) {
        auto& registry = GetRegistry();
        const std::scoped_lock lock(registry.mutex);
        for (const auto& row : registry.rows) {
            if (row->owner == threadId) {
                for (auto& count : row->counts) {
                    count.store(0, std::memory_order_relaxed);
                }
            }
        }
    }

   private:
    V8InstanceTracker() = default;
};

}  // namespace d2bs::api
