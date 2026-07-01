#pragma once

#include <cstdint>

#include "components/console/Panel.h"

namespace v8 {
class Isolate;
}  // namespace v8

namespace d2bs {
class Script;
}  // namespace d2bs

namespace d2bs::js::console {

// JS call stacks per script. Capture is opt-in: selecting a script raises its
// Script::SetStackCaptureMode to OnYield (refresh at delay() yields) or, with
// the checkbox, OnEveryCall (also on every JS->native callback). Unselected
// scripts stay Off and pay nothing; deselecting or hiding the console drops the
// mode back to Off.
class StacktracesPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Stacktraces"; }
    void Draw() override;

   private:
    // Native thread id of the currently selected script, or 0 when nothing
    // is selected. We key by tid (uint32_t) rather than by pointer so the
    // value stays valid across script restarts and pointer reuse.
    uint32_t selectedTid_ = 0;

    // Selected script's capture tier: on -> OnEveryCall (refresh on every native
    // callback - live but costly), off -> OnYield (refresh only at delay()
    // yields). Off by default.
    bool captureOnEveryCall_ = false;
};

}  // namespace d2bs::js::console
