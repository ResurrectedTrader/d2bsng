#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include <imgui.h>

namespace d2bs::js::console {

// Line-granular text selection for the console's scrolling text views
// (LogPanel scrollback, ConsolePanel transcript). Those panels render colored,
// per-segment text via ImGui::Text* calls, which are not natively selectable.
// RowSelection overlays an invisible, full-width ImGui::Selectable under each
// rendered line so the user can click / shift-click / drag to select whole
// lines, then Ctrl+C (or a panel's Copy button) to copy them.
//
// Rows are identified by a caller-assigned stable uint64 id - a monotonic
// sequence number per log entry; multi-line entries fold the line index into
// the id. Selection state is keyed by id, so it survives new lines arriving and
// old lines being evicted from the front of the buffer.
//
// The panel owns the text, so RowSelection never builds the clipboard string
// itself: it reports which ids are selected (IsSelected) and whether a copy was
// requested this frame (CopyRequested). The panel assembles the text, reusing
// its existing StripColor formatting.
//
// Usage per frame, inside the BeginChild() that holds the scroll region:
//
//   selection_.Begin();
//   for (each row in the filtered view) {
//       if (!selection_.Row(id)) continue;   // off-screen: layout reserved, skip draw
//       ... draw colored text on top ...
//   }
//   selection_.End();
//   if (selection_.CopyRequested()) { ... copy rows where IsSelected(id) ... }
//
// Begin()/End() must bracket the row loop with NO early return between them:
// together they hold a draw-list channel split plus a pushed style-color pair
// that End() balances. Render-thread only (all ImGui calls).
class RowSelection {
   public:
    // Reset per-frame state and read keyboard intents (Ctrl+A / Ctrl+C /
    // Escape, gated on the host child being focused). Call once, right after
    // BeginChild and before the row loop.
    void Begin();

    // Register one row (always - so select-all and range cover the whole view),
    // and, if it is on-screen, draw its selectable and rewind the cursor so the
    // caller can draw colored text on top. Returns false when the row is
    // off-screen: its layout height is reserved (scroll stays correct) but the
    // caller must skip drawing it. Cheap-skipping off-screen rows is what keeps
    // a large buffer affordable without a clipper that would lose row ids.
    [[nodiscard]] bool Row(uint64_t id);

    // Resolve this frame's click / shift / drag into the selection set, and
    // apply any pending select-all / clear. Call once after the row loop.
    void End();

    [[nodiscard]] bool IsSelected(uint64_t id) const { return selected_.contains(id); }
    [[nodiscard]] bool HasSelection() const { return !selected_.empty(); }

    // True for one frame after the user pressed Ctrl+C over the focused view
    // with a non-empty selection. The panel consumes this to copy the selected
    // rows; an empty selection is a no-op (the panel's Copy button still copies
    // the whole view).
    [[nodiscard]] bool CopyRequested() const { return copyRequested_; }

    // Whether a drag-select gesture is in progress (left button pressed on a row
    // and still held). The panel ANDs this out of its auto-follow condition so a
    // drag - including the auto-scroll past an edge - isn't yanked to the bottom.
    [[nodiscard]] bool IsDragging() const { return dragActive_; }

    // Select every row in the view - the whole buffer, or the whole filtered set
    // when a filter is active (so the highlight always matches what a copy
    // produces). NOT limited to on-screen rows: the panel feeds Row() every
    // filtered entry, scrolled in or not. Safe to call after the row loop (e.g.
    // from a context-menu item); also the Ctrl+A path.
    void SelectAll();

    void Clear();

   private:
    // Replace the selection with the inclusive visual range between the rows
    // carrying fromId and toId. Falls back to a single-row selection at toId if
    // the anchor is no longer visible (scrolled off / filtered out).
    void SelectRange(uint64_t fromId, uint64_t toId);

    // While drag-selecting, scroll the host child when the cursor nears the top
    // or bottom edge (speed scales with how far past the trigger zone it is).
    // Returns -1 (scrolled up), +1 (scrolled down), or 0 (no scroll).
    int32_t AutoScroll();

    // Emit the currently-open highlight run as a single filled rect in the
    // background draw channel, then close it. No-op if no run is open.
    void FlushHighlightRun();

    std::unordered_set<uint64_t> selected_;
    std::vector<uint64_t> frameOrder_;  // ids in visual order, rebuilt each frame

    uint64_t anchorId_ = 0;
    bool hasAnchor_ = false;

    // Active drag-select gesture: set on press over a row, cleared on release.
    // Persists across frames (read at frame start via IsDragging()).
    bool dragActive_ = false;

    // First / last rows that were actually on-screen this frame (ImItemVisible),
    // in visual order. Used to extend the selection to the edge while auto-scrolling.
    uint64_t firstVisibleId_ = 0;
    uint64_t lastVisibleId_ = 0;
    bool firstVisibleValid_ = false;
    bool lastVisibleValid_ = false;

    // The contiguous block of selected, on-screen rows being coalesced into one
    // highlight rect this frame. One rect per block (drawn behind the text via a
    // draw-list channel) means no seams where adjacent per-row rects would meet.
    ImVec2 runMin_;
    ImVec2 runMax_;
    bool runOpen_ = false;

    // Captured during Row(), resolved in End() once the full frame order is
    // known (a drag/shift range needs every row's position).
    uint64_t pendingClickId_ = 0;
    uint64_t pendingDragId_ = 0;
    bool pendingClick_ = false;
    bool pendingClickShift_ = false;
    bool pendingDrag_ = false;

    // Keyboard intents captured in Begin(), applied in End().
    bool wantSelectAll_ = false;
    bool wantClear_ = false;
    bool copyRequested_ = false;
};

}  // namespace d2bs::js::console
