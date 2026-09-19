#pragma once

#include <string_view>

#include "components/script/ScriptRef.h"

namespace d2bs {

class BaseEvent {
    friend class js::script::Invocation;

   protected:
    BaseEvent() = default;
    virtual void MakeArgs(js::script::CallArgs& args) const = 0;

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

    virtual void Execute(js::script::Invocation& call) { call.Run(*this); }
};

}  // namespace d2bs
