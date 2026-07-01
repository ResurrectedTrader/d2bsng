#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>

namespace d2bs::api {

// Transparent comparator map type - allows heterogeneous find/contains with string_view.
using ClassCountMap = std::map<std::string, int32_t, std::less<>>;

// Central registry for per-thread, per-class V8 instance counts.
// Tracks how many live V8-wrapped native objects exist for each class type on each script thread.
// Used for debugging, leak detection, and polling for count==0 during isolate teardown.
class V8InstanceTracker {
   public:
    static V8InstanceTracker& Instance() {
        static V8InstanceTracker tracker;
        return tracker;
    }

    void Increment(std::string_view className) {
        std::scoped_lock lock(mutex_);
        auto& perClass = counts_[std::this_thread::get_id()];
        auto it = perClass.find(className);
        if (it != perClass.end()) {
            ++it->second;
        } else {
            perClass.emplace(std::string(className), 1);
        }
    }

    void Decrement(std::string_view className) {
        std::scoped_lock lock(mutex_);
        auto& perClass = counts_[std::this_thread::get_id()];
        auto it = perClass.find(className);
        if (it != perClass.end()) {
            --it->second;
        }
    }

    // Returns a snapshot of per-class counts (only non-zero entries).
    // If threadId is provided, returns counts for that thread only; otherwise sums across all threads.
    ClassCountMap Snapshot(std::optional<std::thread::id> threadId = std::nullopt) const {
        std::scoped_lock lock(mutex_);
        ClassCountMap merged;
        if (threadId) {
            auto it = counts_.find(*threadId);
            if (it != counts_.end()) {
                for (const auto& [name, count] : it->second) {
                    if (count != 0)
                        merged[name] = count;
                }
            }
        } else {
            for (const auto& perClass : counts_ | std::views::values) {
                for (const auto& [name, count] : perClass) {
                    merged[name] += count;
                }
            }
            std::erase_if(merged, [](const auto& entry) { return entry.second == 0; });
        }
        return merged;
    }

    // Remove all entries for a thread (call during isolate teardown after logging any leaks)
    void ClearThread(std::thread::id threadId) {
        std::scoped_lock lock(mutex_);
        counts_.erase(threadId);
    }

   private:
    V8InstanceTracker() = default;

    // thread::id -> (className -> count)
    std::map<std::thread::id, ClassCountMap> counts_;
    mutable std::mutex mutex_;
};

}  // namespace d2bs::api
