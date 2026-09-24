#pragma once

#include <spdlog/logger.h>
#include <v8.h>
#include <memory>
#include <string_view>
#include <vector>

#include "components/script/ScriptLogger.h"

namespace d2bs {

class BaseEvent {
   protected:
    BaseEvent() = default;
    virtual std::vector<v8::Local<v8::Value>> MakeArgs(v8::Isolate* isolate) const = 0;

   public:
    virtual ~BaseEvent() = default;

    BaseEvent(const BaseEvent&) = delete;
    BaseEvent& operator=(const BaseEvent&) = delete;
    BaseEvent(BaseEvent&&) noexcept = default;
    BaseEvent& operator=(BaseEvent&&) noexcept = default;

    [[nodiscard]] virtual std::string_view Name() const = 0;

    // Called when the event was dispatched to a script but the script died before processing.
    // Override to clean up dispatch-tracking state (e.g., BlockableEvent decrements remaining_).
    virtual void OnDropped() {}

    virtual void Execute(v8::Isolate* isolate, const std::vector<v8::Local<v8::Function>>& fns) {
        v8::HandleScope scope(isolate);
        auto args = MakeArgs(isolate);
        auto cx = isolate->GetCurrentContext();
        for (const auto& fn : fns) {
            if (!fn.IsEmpty() && fn->IsFunction()) {
                v8::TryCatch tryCatch(isolate);
                (void)fn->Call(cx, cx->Global(), static_cast<int32_t>(args.size()), args.data());
                if (tryCatch.HasCaught()) {
                    auto message = tryCatch.Message();
                    if (!message.IsEmpty()) {
                        v8::String::Utf8Value errorStr(isolate, message->Get());
                        GetLogger(isolate)->error("[{}] handler exception: {}", Name(),
                                                  std::string(*errorStr, errorStr.length()));
                    }
                }
            }
        }
    }
};

}  // namespace d2bs
