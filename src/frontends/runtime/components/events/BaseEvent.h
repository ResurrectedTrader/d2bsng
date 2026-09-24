#pragma once

#include <spdlog/logger.h>
#include <memory>
#include <string_view>
#include <vector>

#include "components/script/ScriptLogger.h"
#include "unibind/unibind.h"

namespace d2bs {

class BaseEvent {
   protected:
    BaseEvent() = default;
    virtual std::vector<ub::Local<ub::Value>> MakeArgs(const ub::Context& context) const = 0;

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

    virtual void Execute(const ub::Context& context, const std::vector<ub::Local<ub::Function>>& fns) {
        auto& isolate = context.GetIsolate();
        const ub::HandleScope scope(isolate);
        auto args = MakeArgs(context);
        for (const auto& fn : fns) {
            if (!fn.IsEmpty()) {
                const ub::TryCatch tryCatch(isolate);
                (void)fn.Call(context, context.GlobalObject(), args);
                if (tryCatch.HasCaught()) {
                    if (auto message = tryCatch.Message(context)) {
                        GetLogger(&isolate)->error("[{}] handler exception: {}", Name(), *message);
                    }
                }
            }
        }
    }
};

}  // namespace d2bs
