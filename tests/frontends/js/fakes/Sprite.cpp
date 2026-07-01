#include "game/Sprite.h"

// Minimal Sprite fake. No tests currently exercise sprite loading or
// drawing; the fake exists only to satisfy the linker for translation units
// in the framework that reference Sprite (notably ImageDrawable). The
// factories always return nullopt, so every Sprite in test code is the
// default-constructed unloaded sentinel - operator bool returns false,
// Size returns Zero, Draw is a no-op.

namespace d2bs::game {

std::optional<Sprite> Sprite::FromFile(const std::filesystem::path& /*path*/) {
    return std::nullopt;
}

std::optional<Sprite> Sprite::FromMpq(const std::string& /*mpqPath*/) {
    return std::nullopt;
}

Size Sprite::Size() const {
    return Size::Zero;
}

void Sprite::Draw(Point /*centerPos*/, uint32_t /*color*/, bool /*isAutomap*/) const {}

}  // namespace d2bs::game
