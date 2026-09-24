#include "components/inspector/InspectorTarget.h"

#include <utility>

namespace d2bs::runtime::inspector {

InspectorTarget::InspectorTarget(std::string id, std::string title, std::string url,
                                 std::shared_ptr<ub::InspectorDispatcher> dispatcher, ub::JobCallback callback,
                                 ub::CallbackData data)
    : id_(std::move(id)),
      title_(std::move(title)),
      url_(std::move(url)),
      dispatcher_(std::move(dispatcher)),
      dispatchCallback_(callback),
      dispatchData_(data) {}

void InspectorTarget::Push(EventKind kind, std::string payload) {
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(Event{.kind = kind, .payload = std::move(payload)});
    }
    cv_.notify_all();

    // Break a script that's busy in JS so it drains the queue at the next safe
    // point. A plain interrupt would not do: dispatching a CDP message can run
    // script (a DevTools evaluate), which only an inspector dispatch may.
    if (!dispatchRequested_.exchange(true)) {
        dispatcher_->RequestDispatch(dispatchCallback_, dispatchData_);
    }
}

std::deque<InspectorTarget::Event> InspectorTarget::DrainAll() {
    // Fast path: skip the lock when nothing was queued since the last drain (the
    // common per-tick case). Push sets dispatchRequested_ after each enqueue, so
    // false means empty; a racing push is taken on the next drain (and has
    // already requested a dispatch).
    if (!dispatchRequested_.load()) {
        return {};
    }
    std::scoped_lock lock(mutex_);
    dispatchRequested_.store(false);
    std::deque<Event> out;
    out.swap(queue_);
    return out;
}

void InspectorTarget::WaitForEvents(const std::stop_token& stop, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, timeout, [&] { return !queue_.empty() || stop.stop_requested(); });
}

}  // namespace d2bs::runtime::inspector
