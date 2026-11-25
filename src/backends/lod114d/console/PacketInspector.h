#pragma once

// Dev-console tab listing the game packets that cross the receive (P3) and send (P4) intercepts, each decoded through
// its packet class (packets/) or shown as bytes. Direction and id filters, a hex view, mirroring to the log, on-disk
// recording and per-id stack traces are live controls on the tab. See docs/packet_log.md.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

#include "game/Console.h"

namespace d2bs::lod114d::console::packet_inspector {

// Captured packets kept before the oldest is dropped.
inline constexpr uint32_t DEFAULT_HISTORY = 10000;

// Characters each of the tab's id-filter boxes holds.
inline constexpr size_t FILTER_CAPACITY = 1024;

// Starts the recorder: registers the packet observer with the intercepts, so the capture buffer fills whether or not
// the tab is open. Idempotent.
void Install();

// Stops the recorder. Idempotent.
void Remove();

// The console tab. It owns what it shows - the filters, the history size and the column toggles - and reads the
// capture buffer of the recorder Install() started, which outlives it because the intercepts write into it on their
// own threads.
class Panel final : public game::console::BackendPanel {
   public:
    Panel();

    [[nodiscard]] std::string Title() const override;
    void Draw() override;

   private:
    void DrawToolbar();
    void DrawFilters();

    bool showServer_ = false;
    bool showClient_ = false;
    bool showHex_ = false;
    bool hideHandled_ = false;
    bool logToConsole_ = false;
    bool recording_ = false;
    uint32_t history_ = DEFAULT_HISTORY;

    std::array<char, FILTER_CAPACITY> excludeText_{};
    std::array<char, FILTER_CAPACITY> includeText_{};
    std::array<char, FILTER_CAPACITY> stacktraceText_{};
    std::unordered_set<uint8_t> excluded_;
    std::unordered_set<uint8_t> included_;
    std::unordered_set<uint8_t> stacktraced_;
};

}  // namespace d2bs::lod114d::console::packet_inspector
