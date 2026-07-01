#pragma once

#include <cstdint>
#include <string>

#include "components/console/Panel.h"

namespace d2bs::js::console {

// Lists every native thread in the current process with its Win32 thread
// description. Pick a thread in the combo and press "Capture" to walk its
// stack via SuspendThread / StackWalk64; the result lands in a read-only
// multiline field with a Copy button.
//
// Enumeration runs every frame via CreateToolhelp32Snapshot - cheap enough
// for a debug panel and keeps the list fresh as threads come and go.
class ThreadsPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Threads"; }
    void Draw() override;

   private:
    uint32_t selectedTid_ = 0;
    uint32_t capturedTid_ = 0;
    std::string capturedDescription_;
    std::string capturedStack_;
};

}  // namespace d2bs::js::console
