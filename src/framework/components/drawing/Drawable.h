// d2bs/components/drawing/Drawable.h
#pragma once

#include <v8.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "components/config/AppConfig.h"
#include "game/Sprite.h"
#include "game/Types.h"

namespace d2bs::framework::drawing {

enum class Align : uint8_t { Left, Right, Center };

struct Drawable : std::enable_shared_from_this<Drawable> {
    // 8-byte POD atomics - lock-free on x86 via CMPXCHG8B.  The asserts
    // make any platform regression a compile error.
    static_assert(std::atomic<d2bs::game::Point>::is_always_lock_free,
                  "std::atomic<Point> must be lock-free on the target platform");
    // Default (0,0) matches pre-refactor JS-observable defaults (`new Box().x === 0`).
    // Scripts may still explicitly assign -1; the sentinel check in Draw() honours that.
    std::atomic<d2bs::game::Point> pos{d2bs::game::Point::Zero};
    std::atomic<int32_t> zorder = 1;
    std::atomic<Align> align = Align::Left;
    std::atomic<bool> isVisible = true;
    std::atomic<bool> isAutomap = false;
    std::atomic<bool> isHovered = false;

    // Optional cleanup hook invoked from ~Drawable. Populated by the JS wrapper
    // layer (JSDrawableBase::SetupInstanceTracking) to decrement the per-thread
    // V8 instance count without pulling an api/ dependency into components/.
    std::function<void()> onDestroy;

    v8::Global<v8::Function> onClick;
    v8::Global<v8::Function> onHover;

    virtual ~Drawable();

    virtual void Draw() const = 0;
    virtual bool Contains(d2bs::game::Point p) const = 0;

    // Game-thread collection entry points. Each iterates live scripts via
    // Script::GetDrawables(), which returns a snapshot with per-script isolate
    // keep-alive so ~Drawable's v8::Global::Reset is safe under concurrent
    // script teardown.
    static void DrawAll(d2bs::game::GameState state);

    // Returns true if a visible drawable with an onClick handler contains point.
    // Called from the game thread to decide whether to block the game's click handling.
    // Also posts a ScreenHookClickEvent to the owning script for V8 callback invocation.
    static bool OnClick(d2bs::game::ClickButton button, d2bs::game::Point pos, d2bs::game::GameState state);

    // Tracks hover enter/leave state for all drawables.  Respects z-order:
    // only the topmost visible drawable with an onHover handler is considered
    // "hovered".  Posts ScreenHookHoverEvents to owning scripts on transitions.
    static void OnMouseMove(d2bs::game::Point pos, d2bs::game::GameState state);
};

struct BoxDrawable final : Drawable {
    static_assert(std::atomic<d2bs::game::Size>::is_always_lock_free,
                  "std::atomic<Size> must be lock-free on the target platform");
    std::atomic<d2bs::game::Size> size{d2bs::game::Size::Zero};
    std::atomic<uint32_t> color = 0;
    std::atomic<uint32_t> opacity = 0;

    void Draw() const override;
    bool Contains(d2bs::game::Point p) const override;
};

struct FrameDrawable final : Drawable {
    std::atomic<d2bs::game::Size> size{d2bs::game::Size::Zero};

    void Draw() const override;
    bool Contains(d2bs::game::Point p) const override;
};

struct LineDrawable final : Drawable {
    std::atomic<d2bs::game::Point> p2{d2bs::game::Point::Zero};
    std::atomic<uint32_t> color = 0;

    void Draw() const override;
    bool Contains(d2bs::game::Point p) const override;
};

struct TextDrawable final : Drawable {
    std::atomic<int32_t> font = 0;
    std::atomic<uint32_t> color = 0;

    std::string GetText() const {
        std::scoped_lock lock(textMutex_);
        return text_;
    }
    void SetText(const std::string& value) {
        std::scoped_lock lock(textMutex_);
        text_ = value;
    }

    void Draw() const override;
    bool Contains(d2bs::game::Point p) const override;

   private:
    std::string text_;
    mutable std::mutex textMutex_;
};

struct ImageDrawable final : Drawable {
    std::atomic<uint32_t> color = 0;

    std::filesystem::path GetPath() const {
        std::scoped_lock lock(spriteMutex_);
        return path_;
    }
    // Sandbox-resolves `value` against the script base via
    // AppConfig::GetPathRelScript, then tries the resolved absolute path
    // as a filesystem load and falls back to MPQ with the original
    // (unresolved) path on miss. Both legs failing leaves sprite_ as the
    // unloaded sentinel - Draw / Contains silently no-op.
    void SetPath(const std::filesystem::path& value) {
        auto resolved = d2bs::config::GetPathRelScript(value.string());
        auto loaded = d2bs::game::Sprite::FromFile(resolved);
        if (!loaded) {
            loaded = d2bs::game::Sprite::FromMpq(value.string());
        }
        std::scoped_lock lock(spriteMutex_);
        path_ = value;
        sprite_ = loaded.value_or(d2bs::game::Sprite{});
    }

    void Draw() const override;
    bool Contains(d2bs::game::Point p) const override;

   private:
    // Path retained alongside the sprite handle so GetPath remains cheap
    // and matches the script-visible `image.location` property - note this
    // is the script-supplied (unresolved) path; sprite_ holds the resolved
    // absolute path.
    std::filesystem::path path_;
    d2bs::game::Sprite sprite_;
    mutable std::mutex spriteMutex_;
};

}  // namespace d2bs::framework::drawing
