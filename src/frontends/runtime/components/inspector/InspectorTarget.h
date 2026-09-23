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

namespace v8 {
class Isolate;
}  // namespace v8

namespace d2bs::js::inspector {

// A single debuggable script target registered with the InspectorServer.
//
// Shared (shared_ptr) between the server's WebSocket connection threads
// (producers) and the script's isolate thread (the sole consumer). It owns
// nothing V8 - just a thread-safe queue of inbound Chrome DevTools Protocol
// (CDP) events plus the identity shown in chrome://inspect. Keeping the queue
// here rather than on ScriptInspector lets the server keep delivering and
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

    InspectorTarget(std::string id, std::string title, std::string url, std::weak_ptr<v8::Isolate> isolate);

    [[nodiscard]] const std::string& Id() const { return id_; }
    [[nodiscard]] const std::string& Title() const { return title_; }
    [[nodiscard]] const std::string& Url() const { return url_; }

    // Producer side (WebSocket connection thread). Enqueues an event and wakes
    // the consumer: notifies the queue CV (for a blocked pause loop) and, if the
    // isolate is still alive, schedules a V8 interrupt so a busy script drains
    // soon instead of only at its next delay().
    void Push(EventKind kind, std::string payload = {});

    // Consumer side (isolate thread). Atomically takes the whole pending queue
    // and re-arms the interrupt.
    [[nodiscard]] std::deque<Event> DrainAll();

    // Consumer side (isolate thread, pause loop). Blocks until an event is
    // queued, `stop` is requested, or `timeout` elapses; returns immediately if
    // events are already pending.
    void WaitForEvents(const std::stop_token& stop, std::chrono::milliseconds timeout);

   private:
    const std::string id_;
    const std::string title_;
    const std::string url_;
    const std::weak_ptr<v8::Isolate> isolate_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Event> queue_;
    std::atomic<bool> interruptScheduled_{false};
};

}  // namespace d2bs::js::inspector
