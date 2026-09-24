#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>

#include "unibind/unibind.h"

namespace d2bs::runtime::inspector {

// A single debuggable script target registered with the InspectorServer.
//
// Shared (shared_ptr) between the server's WebSocket connection threads
// (producers) and the script's isolate thread (the sole consumer). It owns
// nothing of the engine - just a thread-safe queue of inbound Chrome DevTools
// Protocol (CDP) events plus the identity shown in chrome://inspect. Keeping the
// queue here rather than on ScriptInspector lets the server keep delivering and
// holding the target across the brief window where the isolate thread is
// tearing the ScriptInspector down.
class InspectorTarget {
   public:
    enum class EventKind : uint8_t {
        Connected,     // a DevTools client attached
        Message,       // a CDP message arrived (payload set)
        Disconnected,  // the DevTools client detached
    };
    struct Event {
        EventKind kind;
        std::string payload;
    };

    // `dispatcher` is how Push wakes a busy script: `callback` is requested
    // through it so it runs on the isolate thread at the next safe point. The
    // dispatcher outlives the ub::Inspector it came from and declines once that
    // is gone, so the target needs no detaching.
    InspectorTarget(std::string id, std::string title, std::string url,
                    std::shared_ptr<ub::InspectorDispatcher> dispatcher, ub::JobCallback callback,
                    ub::CallbackData data);

    [[nodiscard]] const std::string& Id() const { return id_; }
    [[nodiscard]] const std::string& Title() const { return title_; }
    [[nodiscard]] const std::string& Url() const { return url_; }

    // Producer side (WebSocket connection thread). Enqueues an event and wakes
    // the consumer: notifies the queue CV (for a blocked pause loop) and
    // requests a dispatch so a busy script drains soon instead
    // of only at its next delay().
    void Push(EventKind kind, std::string payload = {});

    // Consumer side (isolate thread). Atomically takes the whole pending queue
    // and re-arms the dispatch request.
    [[nodiscard]] std::deque<Event> DrainAll();

    // Consumer side (isolate thread, pause loop). Blocks until an event is
    // queued, `stop` is requested, or `timeout` elapses; returns immediately if
    // events are already pending.
    void WaitForEvents(const std::stop_token& stop, std::chrono::milliseconds timeout);

   private:
    const std::string id_;
    const std::string title_;
    const std::string url_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Event> queue_;
    std::atomic<bool> dispatchRequested_{false};

    const std::shared_ptr<ub::InspectorDispatcher> dispatcher_;
    const ub::JobCallback dispatchCallback_;
    const ub::CallbackData dispatchData_;
};

}  // namespace d2bs::runtime::inspector
