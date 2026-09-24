#pragma once

#include <atomic>
#include "BaseEvent.h"

namespace d2bs {

class DelayedEvent : public BaseEvent {
    inline static std::atomic_uint32_t globalEventId_ = 0;

   protected:
    std::vector<ub::Local<ub::Value>> MakeArgs(const ub::Context& /*context*/) const override {
        // DelayedEvent uses its own callback directly, not MakeArgs
        return {};
    }

   public:
    DelayedEvent(ub::Global<ub::Function> callback, uint32_t repeatMs = 0)
        : eventId_(++globalEventId_), repeatMs_(repeatMs), callback_(std::move(callback)) {}

    [[nodiscard]] uint32_t EventId() const { return eventId_; }
    [[nodiscard]] uint32_t RepeatMs() const { return repeatMs_; }
    [[nodiscard]] bool IsCancelled() const { return cancelled_; }
    void Cancel() { cancelled_ = true; }

    /// Reset the ub::Global callback while the isolate is still alive.
    /// Must be called during teardown before isolate disposal.
    void Invalidate() {
        cancelled_ = true;
        callback_.Reset();
    }

    void Execute(const ub::Context& context, const std::vector<ub::Local<ub::Function>>& /*fns*/) override {
        if (cancelled_ || callback_.IsEmpty())
            return;
        auto& isolate = context.GetIsolate();
        const ub::HandleScope handleScope(isolate);
        const ub::TryCatch tryCatch(isolate);
        auto fn = callback_.Get(isolate);
        (void)fn.Call(context, context.GlobalObject(), {});
        if (tryCatch.HasCaught()) {
            if (auto message = tryCatch.Message(context)) {
                GetLogger(&isolate)->error("[{}] handler exception: {}", Name(), *message);
            }
        }
    }

    [[nodiscard]] std::string_view Name() const override { return repeatMs_ > 0 ? "setInterval" : "setTimeout"; }

   private:
    const uint32_t eventId_;
    const uint32_t repeatMs_;
    std::atomic_bool cancelled_ = false;
    ub::Global<ub::Function> callback_;
};

}  // namespace d2bs
