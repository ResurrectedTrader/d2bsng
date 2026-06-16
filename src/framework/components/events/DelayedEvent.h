#pragma once

#include <atomic>
#include <cstdint>
#include "BaseEvent.h"

namespace d2bs {

class DelayedEvent : public BaseEvent {
    inline static std::atomic_uint32_t globalEventId_ = 0;

   protected:
    std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* /*isolate*/) const override {
        // DelayedEvent uses its own callback directly, not MakeArgs
        return {};
    }

   public:
    DelayedEvent(v8::Global<v8::Function> callback, uint32_t repeatMs = 0)
        : eventId_(++globalEventId_), repeatMs_(repeatMs), callback_(std::move(callback)) {}

    [[nodiscard]] uint32_t EventId() const { return eventId_; }
    [[nodiscard]] uint32_t RepeatMs() const { return repeatMs_; }
    [[nodiscard]] bool IsCancelled() const { return cancelled_; }
    void Cancel() { cancelled_ = true; }

    /// Reset the v8::Global callback while the isolate is still alive.
    /// Must be called during teardown before isolate disposal.
    void Invalidate() {
        cancelled_ = true;
        callback_.Reset();
    }

    void Execute(v8::Isolate* isolate, const std::vector<v8::Local<v8::Function>>& /*fns*/) override {
        if (cancelled_ || callback_.IsEmpty())
            return;
        v8::HandleScope handleScope(isolate);
        v8::TryCatch tryCatch(isolate);
        auto fn = callback_.Get(isolate);
        auto cx = isolate->GetCurrentContext();
        (void)fn->Call(cx, cx->Global(), 0, nullptr);
        if (tryCatch.HasCaught()) {
            auto message = tryCatch.Message();
            if (!message.IsEmpty()) {
                v8::String::Utf8Value errorStr(isolate, message->Get());
                GetLogger(isolate)->error("[{}] handler exception: {}", Name(),
                                          std::string(*errorStr, errorStr.length()));
            }
        }
    }

    [[nodiscard]] std::string_view Name() const override { return repeatMs_ > 0 ? "setInterval" : "setTimeout"; }

   private:
    const uint32_t eventId_;
    const uint32_t repeatMs_;
    std::atomic_bool cancelled_ = false;
    v8::Global<v8::Function> callback_;
};

}  // namespace d2bs
