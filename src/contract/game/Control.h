#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/HandleCache.h"
#include "game/Types.h"

namespace d2bs::game {

// Identity-based handle for D2WinControlStrc*.
// Stores (type, bounds) as identity and resolves to a game pointer on demand
// with per-frame caching via HandleCache.
class Control {
    ControlType type_ = ControlType::Unknown;
    Rect bounds_;
    HandleCache cache_;

    void* ResolvePtr() const;

   public:
    explicit Control(ControlType type = ControlType::Unknown, Rect bounds = {}) : type_(type), bounds_(bounds) {}
    explicit operator bool() const;

    // Extracts identity from a raw D2WinControlStrc pointer.
    static Control FromPtr(void* p);

    // Properties
    std::string Text() const;
    void SetText(const std::string& text) const;
    Rect Bounds() const;
    uint32_t State() const;
    void SetState(uint32_t value) const;
    bool IsPassword() const;
    ControlType Type() const;
    // Text-caret character offset within the EditBox wide-string buffer
    // (D2WinControlStrc::dwCursorPos). Not a 2D position.
    uint32_t CursorPos() const;
    void SetCursorPos(uint32_t offset) const;
    uint32_t SelectStart() const;
    uint32_t SelectEnd() const;
    // Reference-parity locale match against the resolved locale string: Buttons
    // exact-match their text; TextBoxes substring-match pFirstText. False for
    // control types with no locale text source.
    bool HasLocaleText(int32_t localeId) const;

    // Methods
    Control GetNext() const;
    // Click at an optional screen position, with per-axis sentinel support matching
    // the reference behavior:
    //   nullopt        -> both axes use control default (center of control).
    //   pos->x == ~0u  -> X uses control default; Y is pos->y.
    //   pos->y == ~0u  -> Y uses control default; X is pos->x.
    // The UINT32_MAX sentinel is how JS `-1` arrives here after ECMAScript ToUint32
    // in the binding, matching reference clickControl (reference/d2bs/Control.cpp:118-121)
    // which checks `if (x == -1) x = default` per-axis independently.
    void Click(std::optional<Position> pos = std::nullopt) const;
    // Each entry is one ControlText node with 5 wText slots (nullopt for null slots).
    static constexpr size_t TEXT_SLOTS = 5;
    using TextLine = std::array<std::optional<std::string>, TEXT_SLOTS>;
    std::vector<TextLine> TextLines() const;

    static std::optional<Control> GetFirst();

    // === Framework-impl (defined inline in Finders.h) ===
    // Per-axis optionals: JS getControl(type, x, y, xsize, ysize) allows partial args, so each axis filters
    // independently.
    static std::optional<Control> Find(std::optional<ControlType> type = std::nullopt,
                                       std::optional<uint32_t> x = std::nullopt,
                                       std::optional<uint32_t> y = std::nullopt,
                                       std::optional<uint32_t> xsize = std::nullopt,
                                       std::optional<uint32_t> ysize = std::nullopt,
                                       std::optional<int32_t> localeId = std::nullopt);
};

}  // namespace d2bs::game
