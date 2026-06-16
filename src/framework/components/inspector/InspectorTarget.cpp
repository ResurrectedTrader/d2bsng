#include "components/inspector/InspectorTarget.h"

#include <v8.h>

#include <utility>

#include "components/inspector/ScriptInspector.h"

namespace d2bs::framework::inspector {

InspectorTarget::InspectorTarget(std::string id, std::string title, std::string url, std::weak_ptr<v8::Isolate> isolate)
    : id_(std::move(id)), title_(std::move(title)), url_(std::move(url)), isolate_(std::move(isolate)) {}

void InspectorTarget::Push(EventKind kind, std::string payload) {
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(Event{.kind = kind, .payload = std::move(payload)});
    }
    cv_.notify_all();

    // Break a script that's busy in JS so it drains the queue at the next safe
    // point. Holding a shared_ptr across the call keeps the isolate alive even
    // if teardown races us - the custom deleter is deferred until we drop it.
    if (auto isolate = isolate_.lock()) {
        if (!interruptScheduled_.exchange(true)) {
            isolate->RequestInterrupt(
                +[](v8::Isolate* iso, void* /*data*/) {
                    // Runs on the isolate's own thread at a V8 safe point. Resolve the
                    // ScriptInspector from its isolate data slot (set in the ctor,
                    // cleared in the dtor) so a torn-down inspector simply no-ops.
                    if (auto* inspector =
                            static_cast<ScriptInspector*>(iso->GetData(ScriptInspector::ISOLATE_DATA_SLOT))) {
                        inspector->DrainIncoming();
                    }
                },
                nullptr);
        }
    }
}

std::deque<InspectorTarget::Event> InspectorTarget::DrainAll() {
    std::scoped_lock lock(mutex_);
    interruptScheduled_.store(false);
    std::deque<Event> out;
    out.swap(queue_);
    return out;
}

void InspectorTarget::WaitForEvents(const std::stop_token& stop, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, timeout, [&] { return !queue_.empty() || stop.stop_requested(); });
}

}  // namespace d2bs::framework::inspector
