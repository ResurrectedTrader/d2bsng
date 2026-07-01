#include "components/console/RowSelection.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace d2bs::js::console {

void RowSelection::Begin() {
    frameOrder_.clear();
    pendingClick_ = false;
    pendingDrag_ = false;
    wantSelectAll_ = false;
    wantClear_ = false;
    copyRequested_ = false;
    firstVisibleValid_ = false;
    lastVisibleValid_ = false;
    runOpen_ = false;

    // The selectable is hit-testing only - we paint the selected-row highlight
    // ourselves (uniform fill, no per-row borders, no highlight on mere hover),
    // so zero out the selectable's hover/active backgrounds for the row loop.
    // Popped in End().
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));

    // Split the draw list: highlights -> channel 0 (background), row text ->
    // channel 1 (foreground). A selected block's rect isn't known until after
    // its rows' text has been emitted, so routing the rect to the background
    // channel keeps it behind that text. Merged in End().
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    // Keyboard shortcuts only while the scroll view is focused (clicking a row
    // focuses it) or hovered. Hover is included so Ctrl+C works even if a plain
    // child doesn't take nav focus on click; the REPL input lives outside this
    // child, so this won't hijack typing there unless the mouse is over the view.
    if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
        const auto& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            wantSelectAll_ = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            copyRequested_ = true;  // gated on a non-empty selection in End()
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            wantClear_ = true;
        }
    }
}

bool RowSelection::Row(uint64_t id) {
    frameOrder_.push_back(id);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowH = ImGui::GetTextLineHeightWithSpacing();

    // Manual cull: rows outside the visible band skip the selectable and (via the
    // caller's early-out on a false return) their text formatting too, but still
    // reserve their layout height so the scrollbar and positions stay correct.
    // The id is already in frameOrder_, so select-all and range selection remain
    // complete despite the skip. A run terminates here just like an unselected row.
    const float winTop = ImGui::GetWindowPos().y;
    const float winBottom = winTop + ImGui::GetWindowSize().y;
    if (pos.y + rowH < winTop || pos.y > winBottom) {
        FlushHighlightRun();
        ImGui::Dummy(ImVec2(0.0F, ImGui::GetTextLineHeight()));  // +ItemSpacing.y == rowH pitch
        return false;
    }

    // Unique ImGui id from both halves: PushID(int) keeps 32 bits, and on a
    // 32-bit build the folded line index would otherwise be truncated away.
    ImGui::PushID(static_cast<int>(id & 0xFFFFFFFFU));
    ImGui::PushID(static_cast<int>(id >> 32));
    // selected=false: the selectable never paints a background (its hover/active
    // styles are zeroed in Begin()); we paint the highlight ourselves below. Width
    // 0 spans the available row width. Its cursor advance is discarded by the
    // rewind below; the overlaid text drives the real row pitch (also rowH).
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0.0F, rowH));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        pendingClick_ = true;
        pendingClickId_ = id;
        pendingClickShift_ = ImGui::GetIO().KeyShift;
    }
    // AllowWhenBlockedByActiveItem: while dragging, the row the drag started on
    // holds the active id and would otherwise suppress hover on the rows the
    // cursor moves over - which are exactly the ones we need to extend to.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        pendingDrag_ = true;
        pendingDragId_ = id;
    }
    // On-screen (we returned above otherwise): track the span so an auto-scrolling
    // drag can extend the selection to the edge-most visible row.
    if (!firstVisibleValid_) {
        firstVisibleId_ = id;
        firstVisibleValid_ = true;
    }
    lastVisibleId_ = id;
    lastVisibleValid_ = true;
    // Coalesce the contiguous block of selected rows: grow one rect while the
    // block continues, flush it (as a single fill) when it ends. One rect per
    // block, so there are no seams where per-row rects would meet.
    if (IsSelected(id)) {
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();
        if (!runOpen_) {
            runOpen_ = true;
            runMin_ = rmin;
            runMax_ = rmax;
        } else {
            runMin_.x = std::min(runMin_.x, rmin.x);
            runMin_.y = std::min(runMin_.y, rmin.y);
            runMax_.x = std::max(runMax_.x, rmax.x);
            runMax_.y = std::max(runMax_.y, rmax.y);
        }
    } else {
        FlushHighlightRun();
    }
    ImGui::PopID();
    ImGui::PopID();

    // Rewind so the caller draws its colored text over the selectable.
    ImGui::SetCursorScreenPos(pos);
    return true;
}

void RowSelection::End() {
    if (wantSelectAll_) {
        SelectAll();
    } else if (pendingClick_) {
        if (pendingClickShift_ && hasAnchor_) {
            SelectRange(anchorId_, pendingClickId_);
        } else {
            selected_.clear();
            selected_.insert(pendingClickId_);
            anchorId_ = pendingClickId_;
            hasAnchor_ = true;
        }
    } else if (wantClear_) {
        Clear();
    }

    // A drag gesture starts on a press over a row and ends when the left button
    // is released.
    if (pendingClick_) {
        dragActive_ = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragActive_ = false;
    }

    if (dragActive_ && hasAnchor_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::IsMousePosValid()) {
        // Past an edge: scroll, and extend to the edge-most visible row so the
        // selection grows as rows scroll into view. Inside: extend to the
        // hovered row.
        const int32_t scrollDir = AutoScroll();
        if (scrollDir < 0 && firstVisibleValid_) {
            SelectRange(anchorId_, firstVisibleId_);
        } else if (scrollDir > 0 && lastVisibleValid_) {
            SelectRange(anchorId_, lastVisibleId_);
        } else if (pendingDrag_) {
            SelectRange(anchorId_, pendingDragId_);
        }
    }

    if (copyRequested_ && selected_.empty()) {
        copyRequested_ = false;
    }

    FlushHighlightRun();                          // emit the trailing block, if any
    ImGui::GetWindowDrawList()->ChannelsMerge();  // background highlights then text
    ImGui::PopStyleColor(2);                      // HeaderHovered + HeaderActive from Begin()
}

void RowSelection::SelectAll() {
    selected_.clear();
    selected_.insert(frameOrder_.begin(), frameOrder_.end());
    if (!frameOrder_.empty()) {
        anchorId_ = frameOrder_.front();
        hasAnchor_ = true;
    }
}

void RowSelection::FlushHighlightRun() {
    if (!runOpen_) {
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(runMin_, runMax_, ImGui::GetColorU32(ImGuiCol_Header));
    dl->ChannelsSetCurrent(1);
    runOpen_ = false;
}

int32_t RowSelection::AutoScroll() {
    constexpr float SCROLL_SPEED_FACTOR = 20.0F;  // px/sec per px past the trigger zone
    constexpr float SCROLL_SPEED_MAX = 4000.0F;   // px/sec cap

    const ImGuiIO& io = ImGui::GetIO();
    const float mouseY = io.MousePos.y;
    const ImVec2 winPos = ImGui::GetWindowPos();
    const float winTop = winPos.y;
    const float winBottom = winPos.y + ImGui::GetWindowSize().y;
    const float margin = ImGui::GetTextLineHeight();  // trigger zone ~ one line

    int32_t dir = 0;
    float dist = 0.0F;
    if (mouseY > winBottom - margin) {
        dir = 1;
        dist = mouseY - (winBottom - margin);
    } else if (mouseY < winTop + margin) {
        dir = -1;
        dist = (winTop + margin) - mouseY;
    }
    if (dir != 0) {
        const float speed = std::min(dist * SCROLL_SPEED_FACTOR, SCROLL_SPEED_MAX);
        ImGui::SetScrollY(ImGui::GetScrollY() + (static_cast<float>(dir) * speed * io.DeltaTime));
    }
    return dir;
}

void RowSelection::SelectRange(uint64_t fromId, uint64_t toId) {
    const auto itTo = std::ranges::find(frameOrder_, toId);
    if (itTo == frameOrder_.end()) {
        return;  // target not visible (it was just drawn, so this is unexpected)
    }
    const auto itFrom = std::ranges::find(frameOrder_, fromId);
    if (itFrom == frameOrder_.end()) {
        // Anchor scrolled off or got filtered away: restart at the target.
        selected_.clear();
        selected_.insert(toId);
        anchorId_ = toId;
        hasAnchor_ = true;
        return;
    }

    auto lo = itFrom;
    auto hi = itTo;
    if (lo > hi) {
        std::swap(lo, hi);
    }
    selected_.clear();
    for (auto it = lo; it <= hi; ++it) {
        selected_.insert(*it);
    }
}

void RowSelection::Clear() {
    selected_.clear();
    anchorId_ = 0;
    hasAnchor_ = false;
}

}  // namespace d2bs::js::console
