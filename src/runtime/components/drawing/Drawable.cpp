#include "components/drawing/Drawable.h"

#include <algorithm>
#include <ranges>
#include <utility>
#include <vector>

#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "game/GameHelpers.h"

// Threading model:
//
// DrawAll / OnClick / OnMouseMove run on the **game thread**. Each reads a
// per-script shared_ptr<Drawable> vector via Script::GetDrawables() and holds
// those refs locally for the duration of its work. Nothing here touches the
// script engine: hit testing reads the drawable's own hasClick / hasHover
// flags, and handler invocation is handed to the owning Script, which holds
// the callbacks and can drop them the moment the drawable is removed. The
// flags are therefore advisory - a script may clear one between the test and
// the dispatch, and the dispatch then simply does nothing.

namespace d2bs::runtime::drawing {

namespace {

// Shared alignment-offset helper.  `isText` flips the Center/Right behavior
// to match the reference TextHook inversion: text draws its bounding box
// growing left from x=anchor, so the offsets are negative whereas boxes
// and frames split around the anchor.
int32_t AlignOffset(Align a, int32_t width, bool isText = false) {
    if (isText) {
        if (a == Align::Center) {
            return -(width / 2);
        }
        if (a == Align::Right) {
            return -width;
        }
        return 0;
    }
    if (a == Align::Center) {
        return -(width / 2);
    }
    if (a == Align::Right) {
        return width / 2;
    }
    return 0;
}

// Image-specific alignment: the script-supplied (x, y) is the anchor edge
// (Left => left edge; Right => right edge; Center => centre). Sprite::Draw
// expects the centre, so we shift x toward the centre by half-width in the
// non-Center cases. Reference: ScreenHook.cpp:352. (The reference's
// IsInRange on line 364 has an inverted-sign bug for Left alignment; we
// match Draw, not IsInRange.)
int32_t ApplyImageAlign(Align a, int32_t width) {
    if (a == Align::Left) {
        return width / 2;
    }
    if (a == Align::Right) {
        return -(width / 2);
    }
    return 0;
}

struct Hit {
    std::shared_ptr<script::Script> script;
    std::shared_ptr<Drawable> drawable;
};

// Find the topmost visible drawable under `pos` across every script whose
// drawables are visible in `state` and whose drawable satisfies `wants`. The
// caller-supplied predicate filters by handler presence (click / any).
template <typename Wants>
Hit FindTopHit(game::Point pos, game::GameState state, Wants wants) {
    Hit best;
    int32_t bestZ = 0;
    for (auto& script : script::ScriptEngine::Instance().GetAllScripts()) {
        if (!script->DrawablesVisibleIn(state)) {
            continue;
        }
        for (auto& drawable : script->GetDrawables()) {
            if (!drawable->isVisible.load() || !drawable->Contains(pos) || !wants(*drawable)) {
                continue;
            }
            int32_t z = drawable->zorder.load();
            if (!best.drawable || z > bestZ) {
                best = {.script = script, .drawable = drawable};
                bestZ = z;
            }
        }
    }
    return best;
}

}  // namespace

Drawable::~Drawable() {
    if (onDestroy) {
        onDestroy();
    }
}

void Drawable::DrawAll(game::GameState state) {
    std::vector<std::pair<int32_t, std::shared_ptr<Drawable>>> toDraw;
    for (auto& script : script::ScriptEngine::Instance().GetAllScripts()) {
        if (!script->DrawablesVisibleIn(state)) {
            continue;
        }
        for (auto& drawable : script->GetDrawables()) {
            if (!drawable->isVisible.load()) {
                continue;
            }
            if (drawable->isAutomap.load() && !game::GetAutomapOn()) {
                continue;
            }
            toDraw.emplace_back(drawable->zorder.load(), std::move(drawable));
        }
    }

    // Snapshot zorder into the sort vector so the sort key is stable.
    // Drawable::zorder is std::atomic<int32_t>; sorting directly on atomics
    // can violate strict weak ordering if values change mid-sort (UB).
    std::ranges::sort(toDraw, {}, &std::pair<int32_t, std::shared_ptr<Drawable>>::first);

    for (auto& drawable : toDraw | std::views::values) {
        drawable->Draw();
    }
}

bool Drawable::OnClick(game::ClickButton button, game::Point pos, game::GameState state) {
    auto hit = FindTopHit(pos, state, [](const Drawable& d) { return d.hasClick.load(); });
    if (!hit.drawable) {
        return false;
    }
    return hit.script->DispatchDrawableClick(hit.drawable, button, pos);
}

void Drawable::OnMouseMove(game::Point pos, game::GameState state) {
    // Find the topmost visible drawable at pos regardless of its hover handler -
    // occlusion respects z-order over all drawables, so a non-hoverable
    // overlay still blocks hover events on a hoverable drawable below it.
    // Then walk every drawable in matching scripts and flip its isHovered
    // flag, dispatching enter/leave events for transitions. Drawables without
    // a hover handler are skipped in the flip pass - they can't fire an
    // event and don't need flag tracking. JS callbacks run asynchronously on
    // their owning script's thread so re-entrance into Add/RemoveDrawable is
    // safe.
    auto top = FindTopHit(pos, state, [](const Drawable&) { return true; });

    for (auto& script : script::ScriptEngine::Instance().GetAllScripts()) {
        if (!script->DrawablesVisibleIn(state)) {
            continue;
        }
        if (!script->IsAlive()) {
            // Script is tearing down - drawables_ is already cleared (or about
            // to be), no meaningful hover work to do for this script.
            continue;
        }
        for (auto& drawable : script->GetDrawables()) {
            if (!drawable->hasHover.load()) {
                continue;
            }
            bool shouldBeHovered = drawable.get() == top.drawable.get();
            bool expected = !shouldBeHovered;
            if (!drawable->isHovered.compare_exchange_strong(expected, shouldBeHovered)) {
                continue;
            }
            script->DispatchDrawableHover(drawable, shouldBeHovered ? pos : game::Point::Zero, shouldBeHovered);
        }
    }
}

void BoxDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1.0F || snapPos.y == -1.0F) {
        return;
    }
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    game::PointF draw = snapPos + game::Point{.x = AlignOffset(snapAlign, w), .y = 0};
    game::PointF draw2 = draw + game::Point{.x = w, .y = h};
    if (isAutomap.load()) {
        draw = d2bs::game::ScreenToAutomap(draw);
        draw2 = d2bs::game::ScreenToAutomap(draw2);
    }
    d2bs::game::DrawRectangle(draw, draw2, color.load(), opacity.load());
}

bool BoxDrawable::Contains(game::PointF p) const {
    auto snapPos = pos.load();
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    const game::PointF topLeft = snapPos + game::Point{.x = AlignOffset(snapAlign, w), .y = 0};
    const game::PointF bottomRight = topLeft + game::Point{.x = w, .y = h};
    return p.x > topLeft.x && p.x < bottomRight.x && p.y > topLeft.y && p.y < bottomRight.y;
}

void FrameDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1.0F || snapPos.y == -1.0F) {
        return;
    }
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    const game::PointF draw = snapPos + game::Point{.x = AlignOffset(snapAlign, w), .y = 0};
    d2bs::game::DrawFrame(draw, draw + game::Point{.x = w, .y = h});
}

bool FrameDrawable::Contains(game::PointF p) const {
    auto snapPos = pos.load();
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    const game::PointF topLeft = snapPos + game::Point{.x = AlignOffset(snapAlign, w), .y = 0};
    const game::PointF bottomRight = topLeft + game::Point{.x = w, .y = h};
    return p.x > topLeft.x && p.x < bottomRight.x && p.y > topLeft.y && p.y < bottomRight.y;
}

void LineDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1.0F || snapPos.y == -1.0F) {
        return;
    }
    game::PointF draw = snapPos;
    game::PointF draw2 = p2.load();
    if (isAutomap.load()) {
        draw = d2bs::game::ScreenToAutomap(draw);
        draw2 = d2bs::game::ScreenToAutomap(draw2);
    }
    d2bs::game::DrawLine(draw, draw2, color.load(), 0xFF);
}

bool LineDrawable::Contains(game::PointF /*p*/) const {
    return false;  // Lines not clickable
}

void TextDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1.0F || snapPos.y == -1.0F) {
        return;
    }
    std::string snapText = GetText();
    int32_t snapFont = font.load();
    Align snapAlign = align.load();
    auto textSize = game::GetTextSize(snapText, snapFont);
    auto w = static_cast<int32_t>(textSize.width);
    game::PointF draw = snapPos + game::Point{.x = AlignOffset(snapAlign, w, /*isText=*/true), .y = 0};
    if (isAutomap.load()) {
        draw = d2bs::game::ScreenToAutomap(draw);
    }
    d2bs::game::DrawGameText(snapText, draw, color.load(), snapFont);
}

bool TextDrawable::Contains(game::PointF p) const {
    std::string snapText = GetText();
    int32_t snapFont = font.load();
    Align snapAlign = align.load();
    auto snapPos = pos.load();
    auto textSize = game::GetTextSize(snapText, snapFont);
    auto w = static_cast<int32_t>(textSize.width);
    auto h = static_cast<int32_t>(textSize.height);
    const game::PointF baseline = snapPos + game::Point{.x = AlignOffset(snapAlign, w, /*isText=*/true), .y = 0};
    const game::PointF topLeft = baseline - game::Point{.x = 0, .y = h};
    const game::PointF bottomRight = baseline + game::Point{.x = w, .y = 0};
    return p.x >= topLeft.x && p.x < bottomRight.x && p.y >= topLeft.y && p.y < bottomRight.y;
}

void ImageDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1.0F || snapPos.y == -1.0F) {
        return;
    }
    game::Sprite snapSprite;
    {
        std::scoped_lock lock(spriteMutex_);
        snapSprite = sprite_;
    }
    if (!snapSprite) {
        return;
    }
    auto sz = snapSprite.Size();
    auto w = static_cast<int32_t>(sz.width);
    Align snapAlign = align.load();
    const game::PointF center = snapPos + game::Point{.x = ApplyImageAlign(snapAlign, w), .y = 0};
    snapSprite.Draw(center, color.load(), isAutomap.load());
}

bool ImageDrawable::Contains(game::PointF p) const {
    game::Sprite snapSprite;
    {
        std::scoped_lock lock(spriteMutex_);
        snapSprite = sprite_;
    }
    if (!snapSprite) {
        return false;
    }
    auto sz = snapSprite.Size();
    auto w = static_cast<int32_t>(sz.width);
    auto h = static_cast<int32_t>(sz.height);
    auto snapPos = pos.load();
    Align snapAlign = align.load();
    const game::PointF topLeft = snapPos + game::Point{.x = ApplyImageAlign(snapAlign, w) - (w / 2), .y = -(h / 2)};
    const game::PointF bottomRight = topLeft + game::Point{.x = w, .y = h};
    return p.x >= topLeft.x && p.x < bottomRight.x && p.y >= topLeft.y && p.y < bottomRight.y;
}

}  // namespace d2bs::runtime::drawing
