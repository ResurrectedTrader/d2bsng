#pragma once

#include <functional>

namespace d2bs {

// RAII guard that calls a function on destruction unless dismissed.
// Use for cleanup callbacks that should fire unless the happy path completes.
//
// Example:
//   auto guard = std::make_shared<DeferGuard>([&]{ resource.Release(); });
//   DoWork();          // If this throws, guard destructor calls Release()
//   guard->Dismiss();  // Happy path - skip cleanup
class DeferGuard {
    std::function<void()> fn_;
    bool dismissed_ = false;

   public:
    explicit DeferGuard(std::function<void()> fn) : fn_(std::move(fn)) {}
    ~DeferGuard() {
        if (!dismissed_)
            fn_();
    }
    void Dismiss() { dismissed_ = true; }

    DeferGuard(const DeferGuard&) = delete;
    DeferGuard& operator=(const DeferGuard&) = delete;
    DeferGuard(DeferGuard&&) = default;
    DeferGuard& operator=(DeferGuard&&) = default;
};

}  // namespace d2bs
