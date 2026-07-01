#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "game/Types.h"

namespace d2bs::game {

// Value-type handle for a loaded 2D image asset. Construct via FromFile
// or FromMpq factories; default-constructed is the unloaded sentinel.
//
// Internally a Sprite is a thin wrapper around a non-owning pointer into
// the process-lifetime asset cache. Copies share the cached pointer;
// move-from leaves the source empty (compiler-generated default).
//
// Cache: assets are immortal once loaded - no refcounting, no destruction.
// Failed loads are cached as null and never retried, matching reference
// d2bs Vars.mCachedCellFiles.
//
// Cache divergence from Unit/Room/Party: those handles use HandleCache
// (per-frame invalidation) because the underlying game pointers move
// each frame. Sprite assets are immutable disk / MPQ content, so the
// cache is process-lifetime instead.
//
// Anchor convention: Draw() treats `centerPos` as the *centre* of the
// rendered sprite, both horizontally and vertically. Matches reference
// d2bs ImageHook semantics (reference/d2bs/D2Helpers.cpp:585-586).
//
// Multi-frame assets: Size() and Draw() use frame 0 only. Reference does
// the same.
class Sprite {
    void* cached_ = nullptr;

    explicit Sprite(void* loaded) : cached_(loaded) {}

   public:
    Sprite() = default;
    Sprite(const Sprite&) = default;
    Sprite& operator=(const Sprite&) = default;
    Sprite(Sprite&&) noexcept = default;
    Sprite& operator=(Sprite&&) noexcept = default;
    ~Sprite() = default;

    // Load an 8-bit indexed RGB BMP from `path` and cache the result.
    // Returns nullopt for missing files or non-conforming formats. The
    // path must be absolute and pre-validated by the caller - sandbox
    // checks happen in the framework (AppConfig::GetPathRelScript), not
    // here. Subsequent calls with the same path hit the cache without
    // re-reading the file.
    static std::optional<Sprite> FromFile(const std::filesystem::path& path);

    // Load a DC6 cell from D2's MPQ archives via fn::LoadCellFile.
    // `mpqPath` is the MPQ-internal path (e.g., "data\\global\\ui\\foo.dc6").
    // Returns nullopt if the MPQ load fails or the function pointer is
    // unavailable.
    static std::optional<Sprite> FromMpq(const std::string& mpqPath);

    // True iff this handle holds a loaded asset.
    explicit operator bool() const { return cached_ != nullptr; }

    // Frame-0 dimensions in pixels. Zero when !*this.
    Size Size() const;

    // Draw at screen-space `centerPos` (sprite is centred horizontally
    // and vertically on this point) with palette index `color` (low byte;
    // upper bytes ignored). ScreenToAutomap conversion is handled
    // internally when isAutomap is true. No-op when !*this.
    void Draw(Point centerPos, uint32_t color, bool isAutomap) const;
};

}  // namespace d2bs::game
