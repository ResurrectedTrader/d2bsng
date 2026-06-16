// d2bs/components/drawing/Drawable.cpp
#include "components/drawing/Drawable.h"

#include <algorithm>
#include <chrono>
#include <ranges>
#include <utility>
#include <vector>

#include "components/events/Events.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"
#include "game/GameHelpers.h"

// Threading model:
//
// DrawAll / OnClick / OnMouseMove run on the **game thread**. Each
// reads a per-script shared_ptr<Drawable> vector via Script::GetDrawables()
// and holds those refs locally for the duration of its work. v8::Global
// handles on a drawable may be pre-Reset by the owning script thread (via
// Script::RemoveDrawable or Script::TeardownIsolate) while the game thread
// still holds a shared_ptr; the game thread must therefore treat a freshly
// copied Global being empty as "raced teardown" rather than a bug.
//
// Global copy across threads is safe without Locker/Isolate::Scope/HandleScope.
// Global(isolate, source) calls GlobalizeReference, which is a slot allocation,
// not JS execution. If the
// owning thread Reset()s the source concurrently the race is safe on x86:
// we either copy before Reset (valid independent handle) or after (source is
// empty, New returns nullptr, no-op). Aligned pointer reads/writes are atomic.

namespace d2bs::framework::drawing {

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
    std::shared_ptr<d2bs::Script> script;
    std::shared_ptr<Drawable> drawable;
};

// Find the topmost visible drawable under `pos` across every script whose
// drawables are visible in `state` and whose drawable satisfies `wants`. The
// caller-supplied predicate filters by handler presence (onClick / onHover /
// any).
template <typename Wants>
Hit FindTopHit(d2bs::game::Point pos, d2bs::game::GameState state, Wants wants) {
    Hit best;
    int32_t bestZ = 0;
    for (auto& script : d2bs::ScriptEngine::Instance().GetAllScripts()) {
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
    onClick.Reset();
    onHover.Reset();
    if (onDestroy) {
        onDestroy();
    }
}

void Drawable::DrawAll(d2bs::game::GameState state) {
    std::vector<std::pair<int32_t, std::shared_ptr<Drawable>>> toDraw;
    for (auto& script : d2bs::ScriptEngine::Instance().GetAllScripts()) {
        if (!script->DrawablesVisibleIn(state)) {
            continue;
        }
        for (auto& drawable : script->GetDrawables()) {
            if (!drawable->isVisible.load()) {
                continue;
            }
            if (drawable->isAutomap.load() && !d2bs::game::GetAutomapOn()) {
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

bool Drawable::OnClick(d2bs::game::ClickButton button, d2bs::game::Point pos, d2bs::game::GameState state) {
    auto hit = FindTopHit(pos, state, [](const Drawable& d) { return !d.onClick.IsEmpty(); });
    if (!hit.drawable) {
        return false;
    }
    auto iso = hit.script->GetIsolate();
    if (!iso) {
        return false;
    }
    v8::Global<v8::Function> fn(iso.get(), hit.drawable->onClick);
    if (fn.IsEmpty()) {
        return false;  // raced teardown / removal
    }
    auto evt = std::make_shared<d2bs::ScreenHookClickEvent>(button, pos, std::move(fn));
    // Click events are blockable; bump the expected-handler counter so
    // IsBlocked() waits for the JS callback's return value. Hover events are
    // fire-and-forget and do not use this counter.
    evt->IncrementExpected();
    if (!hit.script->ExecuteEvent(evt)) {
        evt->DecrementExpected();
        return false;
    }
    return evt->IsBlocked(std::chrono::seconds(3)).value_or(false);
}

void Drawable::OnMouseMove(d2bs::game::Point pos, d2bs::game::GameState state) {
    // Find the topmost visible drawable at pos regardless of onHover -
    // occlusion respects z-order over all drawables, so a non-hoverable
    // overlay still blocks hover events on a hoverable drawable below it.
    // Then walk every drawable in matching scripts and flip its isHovered
    // flag, dispatching enter/leave events for transitions. Drawables without
    // an onHover handler are skipped in the flip pass - they can't fire an
    // event and don't need flag tracking. JS callbacks run asynchronously on
    // their owning script's thread so re-entrance into Add/RemoveDrawable is
    // safe.
    auto top = FindTopHit(pos, state, [](const Drawable&) { return true; });

    for (auto& script : d2bs::ScriptEngine::Instance().GetAllScripts()) {
        if (!script->DrawablesVisibleIn(state)) {
            continue;
        }
        auto iso = script->GetIsolate();
        if (!iso) {
            // Script is tearing down - drawables_ is already cleared (or about
            // to be), no meaningful hover work to do for this script.
            continue;
        }
        for (auto& drawable : script->GetDrawables()) {
            if (drawable->onHover.IsEmpty()) {
                continue;
            }
            bool shouldBeHovered = drawable.get() == top.drawable.get();
            bool expected = !shouldBeHovered;
            if (!drawable->isHovered.compare_exchange_strong(expected, shouldBeHovered)) {
                continue;
            }
            v8::Global<v8::Function> fn(iso.get(), drawable->onHover);
            if (fn.IsEmpty()) {
                continue;
            }
            script->ExecuteEvent(std::make_shared<d2bs::ScreenHookHoverEvent>(
                shouldBeHovered ? pos : d2bs::game::Point::Zero, shouldBeHovered, std::move(fn)));
        }
    }
}

void BoxDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1 || snapPos.y == -1) {
        return;
    }
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    d2bs::game::Point draw{.x = snapPos.x + AlignOffset(snapAlign, w), .y = snapPos.y};
    d2bs::game::Point draw2{.x = draw.x + w, .y = draw.y + h};
    if (isAutomap.load()) {
        draw = d2bs::game::ScreenToAutomap(draw);
        draw2 = d2bs::game::ScreenToAutomap(draw2);
    }
    d2bs::game::DrawRectangle(draw, draw2, color.load(), opacity.load());
}

bool BoxDrawable::Contains(d2bs::game::Point p) const {
    auto snapPos = pos.load();
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    int32_t hitX = snapPos.x + AlignOffset(snapAlign, w);
    return p.x > hitX && p.x < hitX + w && p.y > snapPos.y && p.y < snapPos.y + h;
}

void FrameDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1 || snapPos.y == -1) {
        return;
    }
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    d2bs::game::Point draw{.x = snapPos.x + AlignOffset(snapAlign, w), .y = snapPos.y};
    d2bs::game::Point draw2{.x = draw.x + w, .y = draw.y + h};
    d2bs::game::DrawFrame(draw, draw2);
}

bool FrameDrawable::Contains(d2bs::game::Point p) const {
    auto snapPos = pos.load();
    auto snapSize = size.load();
    Align snapAlign = align.load();
    auto w = static_cast<int32_t>(snapSize.width);
    auto h = static_cast<int32_t>(snapSize.height);
    int32_t hitX = snapPos.x + AlignOffset(snapAlign, w);
    return p.x > hitX && p.x < hitX + w && p.y > snapPos.y && p.y < snapPos.y + h;
}

void LineDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1 || snapPos.y == -1) {
        return;
    }
    auto snapP2 = p2.load();
    d2bs::game::Point draw = snapPos;
    d2bs::game::Point draw2 = snapP2;
    if (isAutomap.load()) {
        draw = d2bs::game::ScreenToAutomap(draw);
        draw2 = d2bs::game::ScreenToAutomap(draw2);
    }
    d2bs::game::DrawLine(draw, draw2, color.load(), 0xFF);
}

bool LineDrawable::Contains(d2bs::game::Point /*p*/) const {
    return false;  // Lines not clickable
}

void TextDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1 || snapPos.y == -1) {
        return;
    }
    std::string snapText = GetText();
    int32_t snapFont = font.load();
    Align snapAlign = align.load();
    auto textSize = d2bs::game::GetTextSize(snapText, snapFont);
    auto w = static_cast<int32_t>(textSize.width);
    d2bs::game::Point draw{.x = snapPos.x + AlignOffset(snapAlign, w, /*isText=*/true), .y = snapPos.y};
    if (isAutomap.load()) {
        draw = d2bs::game::ScreenToAutomap(draw);
    }
    d2bs::game::DrawGameText(snapText, draw, color.load(), snapFont);
}

bool TextDrawable::Contains(d2bs::game::Point p) const {
    std::string snapText = GetText();
    int32_t snapFont = font.load();
    Align snapAlign = align.load();
    auto snapPos = pos.load();
    auto textSize = d2bs::game::GetTextSize(snapText, snapFont);
    auto w = static_cast<int32_t>(textSize.width);
    int32_t drawX = snapPos.x + AlignOffset(snapAlign, w, /*isText=*/true);
    return p.x >= drawX && p.x < drawX + w && p.y >= snapPos.y - static_cast<int32_t>(textSize.height) &&
           p.y < snapPos.y;
}

void ImageDrawable::Draw() const {
    auto snapPos = pos.load();
    if (snapPos.x == -1 || snapPos.y == -1) {
        return;
    }
    d2bs::game::Sprite snapSprite;
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
    d2bs::game::Point center{.x = snapPos.x + ApplyImageAlign(snapAlign, w), .y = snapPos.y};
    snapSprite.Draw(center, color.load(), isAutomap.load());
}

bool ImageDrawable::Contains(d2bs::game::Point p) const {
    d2bs::game::Sprite snapSprite;
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
    int32_t centerX = snapPos.x + ApplyImageAlign(snapAlign, w);
    int32_t centerY = snapPos.y;
    int32_t left = centerX - (w / 2);
    int32_t top = centerY - (h / 2);
    return p.x >= left && p.x < left + w && p.y >= top && p.y < top + h;
}

}  // namespace d2bs::framework::drawing
