#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "components/console/Panel.h"

namespace d2bs::js::console {

// Lists every native thread in the current process with its Win32 thread
// description. Pick a thread in the combo and press "Capture" to walk its
// stack via SuspendThread / StackWalk64; the result lands in a read-only
// multiline field with a Copy button.
//
// The list is enumerated once when the panel is first drawn and then only on demand, via the
// Refresh button, so a chosen thread stays put while the user works with it.
class ThreadsPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Threads"; }
    void Draw() override;

   private:
    struct Entry {
        uint32_t tid = 0;
        std::string description;
    };

    [[nodiscard]] static std::string FormatLabel(const Entry& entry);
    void Refresh();

    // Empty until the first draw; refreshed only on demand.
    std::optional<std::vector<Entry>> threads_;
    uint32_t selectedTid_ = 0;
    uint32_t capturedTid_ = 0;
    std::string capturedDescription_;
    std::string capturedStack_;
};

}  // namespace d2bs::js::console
