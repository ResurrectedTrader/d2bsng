#pragma once

#include <algorithm>
#include <atomic>
#include <utility>
#include <vector>

#include "BaseEvent.h"

namespace d2bs::runtime::events {

class DelayedEvent : public BaseEvent {
    inline static std::atomic_uint32_t globalEventId_ = 0;

   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* /*isolate*/) const override {
        // DelayedEvent uses its own callback directly, not MakeArgs
        return {};
    }

   public:
    DelayedEvent(v8::Global<v8::Function> callback, std::vector<v8::Global<v8::Value>> args, uint32_t delayMs,
                 bool isRepeating)
        : eventId_(++globalEventId_),
          delayMs_(delayMs),
          isRepeating_(isRepeating),
          callback_(std::move(callback)),
          args_(std::move(args)) {}

    [[nodiscard]] uint32_t EventId() const { return eventId_; }
    [[nodiscard]] bool IsRepeating() const { return isRepeating_; }
    // A zero period still repeats, once per millisecond at most.
    [[nodiscard]] uint32_t PeriodMs() const { return std::max(delayMs_, 1U); }
    [[nodiscard]] bool IsCancelled() const { return cancelled_; }
    void Cancel() { cancelled_ = true; }

    /// Reset the v8::Global handles while the isolate is still alive.
    /// Must be called during teardown before isolate disposal.
    void Invalidate() {
        cancelled_ = true;
        callback_.Reset();
        for (auto& arg : args_) {
            arg.Reset();
        }
    }

    void Execute(v8::Isolate* isolate, const std::vector<v8::Local<v8::Function>>& /*fns*/) override {
        if (cancelled_ || callback_.IsEmpty())
            return;
        v8::HandleScope handleScope(isolate);
        v8::TryCatch tryCatch(isolate);
        auto fn = callback_.Get(isolate);
        auto cx = isolate->GetCurrentContext();
        std::vector<v8::Local<v8::Value>> argv;
        argv.reserve(args_.size());
        for (const auto& arg : args_) {
            argv.push_back(arg.Get(isolate));
        }
        (void)fn->Call(cx, cx->Global(), static_cast<int32_t>(argv.size()), argv.data());
        if (tryCatch.HasCaught()) {
            script::ReportHandlerException(isolate, tryCatch);
        }
    }

    [[nodiscard]] std::string_view Name() const override { return isRepeating_ ? "setInterval" : "setTimeout"; }

   private:
    const uint32_t eventId_;
    const uint32_t delayMs_;
    const bool isRepeating_;
    std::atomic_bool cancelled_ = false;
    v8::Global<v8::Function> callback_;
    std::vector<v8::Global<v8::Value>> args_;
};

}  // namespace d2bs::runtime::events
