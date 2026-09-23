#pragma once

#include <atomic>
#include <cstdint>
#include <utility>
#include "BaseEvent.h"

namespace d2bs {

class DelayedEvent : public BaseEvent {
    inline static std::atomic_uint32_t globalEventId_ = 0;

   protected:
    // The timer callback takes no arguments.
    void MakeArgs(runtime::script::CallArgs& /*args*/) const override {}

   public:
    explicit DelayedEvent(runtime::script::Ref callback, uint32_t repeatMs = 0)
        : eventId_(++globalEventId_), repeatMs_(repeatMs), callback_(std::move(callback)) {}

    [[nodiscard]] uint32_t EventId() const { return eventId_; }
    [[nodiscard]] uint32_t RepeatMs() const { return repeatMs_; }
    [[nodiscard]] bool IsCancelled() const { return cancelled_; }
    void Cancel() { cancelled_ = true; }

    /// Drop the callback while the script that owns it is still alive.
    /// Must be called during teardown, on the script's own thread.
    void Invalidate() {
        cancelled_ = true;
        callback_.Reset();
    }

    void Execute(runtime::script::Invocation& call) override {
        if (cancelled_ || callback_.IsEmpty())
            return;
        call.Run(*this, callback_);
    }

    [[nodiscard]] std::string_view Name() const override { return repeatMs_ > 0 ? "setInterval" : "setTimeout"; }

   private:
    const uint32_t eventId_;
    const uint32_t repeatMs_;
    std::atomic_bool cancelled_ = false;
    runtime::script::Ref callback_;
};

}  // namespace d2bs
