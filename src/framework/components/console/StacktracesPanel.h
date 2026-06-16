#pragma once

#include <cstdint>

#include "components/console/Panel.h"

namespace v8 {
class Isolate;
}  // namespace v8

namespace d2bs {
class Script;
}  // namespace d2bs

namespace d2bs::framework::console {

// JS call stacks per script. Capture is opt-in: pick a script from the
// combo and that script's Script::SetStackCaptureEnabled(true) flag flips
// on. From then on, every JS->native callback (via NativeCallHook
// trampolines) plus every delay() yield refreshes the script's cached
// stack. Selecting a different script disables capture on the previous
// one so unselected scripts pay no overhead.
class StacktracesPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Stacktraces"; }
    void Draw() override;

   private:
    // Native thread id of the currently selected script, or 0 when nothing
    // is selected. We key by tid (uint32_t) rather than by pointer so the
    // value stays valid across script restarts and pointer reuse.
    uint32_t selectedTid_ = 0;

    // When on, the V8Class trampolines refresh the selected script's
    // stack snapshot on every native callback (Method / Property /
    // StaticMethod / global fn). When off, the snapshot only refreshes
    // at delay() yields - free, always running, but stale between yields.
    // Off by default so unselected scripts cost nothing.
    bool captureOnEveryCall_ = false;
};

}  // namespace d2bs::framework::console
