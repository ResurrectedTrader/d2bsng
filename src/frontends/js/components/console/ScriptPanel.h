#pragma once

#include "components/console/Panel.h"

namespace d2bs::js::console {

// Live-script table with per-row Stop / Pause / Resume buttons + header
// actions (Stop all, Reload all, Restart console). Stateless - queries
// ScriptEngine each frame. Script::Stop / Pause / Resume are safe to
// invoke cross-thread.
class ScriptPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Scripts"; }
    void Draw() override;
};

}  // namespace d2bs::js::console
