#include "game/Control.h"

#include <Windows.h>

#include <chrono>
#include <string_view>
#include <thread>

#include "ControlHelpers.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/GameThread.h"
#include "imports/D2Gfx.h"
#include "imports/D2Lang.h"
#include "imports/D2Win.h"
#include "input/InputHook.h"
#include "utils/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2WinButton.h>   // D2WinButtonStrc
#include <D2WinEditBox.h>  // D2WinEditBoxStrc
#include <D2WinTextBox.h>  // D2WinTextBoxStrc, D2WinTextBoxLineStrc
#pragma clang diagnostic pop

#include <cstddef>

namespace d2bs::game {

static_assert(offsetof(D2WinControlStrc, nType) == 0x00);
static_assert(offsetof(D2WinControlStrc, dwFlags) == 0x08);
static_assert(offsetof(D2WinControlStrc, nImageX) == 0x0C);
static_assert(offsetof(D2WinControlStrc, nHeight) == 0x18);
static_assert(offsetof(D2WinControlStrc, pNext) == 0x3C);
static_assert(offsetof(D2WinTextBoxStrc, pFirstLine) == 0x48);
static_assert(offsetof(D2WinEditBoxStrc, nSelectionStart) == 0x54);
static_assert(offsetof(D2WinEditBoxStrc, nSelectionEnd) == 0x58);
static_assert(offsetof(D2WinEditBoxStrc, wszText) == 0x5C);
static_assert(offsetof(D2WinEditBoxStrc, nCaretPosition) == 0x25C);
static_assert(offsetof(D2WinEditBoxStrc, dwEditBoxFlags) == 0x260);
static_assert(offsetof(D2WinButtonStrc, wszText) == 0x64);
static_assert(std::to_underlying(ControlType::EditBox) == D2WIN_EDITBOX);
static_assert(std::to_underlying(ControlType::TextBox) == D2WIN_TEXTBOX);
static_assert(std::to_underlying(ControlType::Button) == D2WIN_BUTTON);

namespace {

D2WinControlStrc* AsCtrl(void* p) noexcept {
    return static_cast<D2WinControlStrc*>(p);
}

// Reference d2bs reads every non-button control through the edit box layout
// (its single Control struct), so Text() / IsPassword() keep doing that.
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) - D2Win controls start with their D2WinControlStrc header
D2WinEditBoxStrc* AsEditBox(D2WinControlStrc* ctrl) noexcept {
    return reinterpret_cast<D2WinEditBoxStrc*>(ctrl);
}

D2WinButtonStrc* AsButton(D2WinControlStrc* ctrl) noexcept {
    return reinterpret_cast<D2WinButtonStrc*>(ctrl);
}

D2WinTextBoxStrc* AsTextBox(D2WinControlStrc* ctrl) noexcept {
    return reinterpret_cast<D2WinTextBoxStrc*>(ctrl);
}
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

void PostMouseMessage(uint32_t msg, int32_t x, int32_t y) {
    auto* hwnd = lod114d::imports::d2gfx::WINDOW_GetWindow();
    if (hwnd == nullptr) {
        return;
    }
    const LPARAM lp = (x & 0xFFFF) | ((y & 0xFFFF) << 16);
    core::input::PostInjectedInput(hwnd, msg, 0, lp);
}

}  // namespace

void* Control::ResolvePtr() const {
    assert(IsGameLockHeld() && "resolve under the game lock - use Resolve<T>()");
    if (type_ == ControlType::Unknown && bounds_ == Rect::Zero) {
        return nullptr;
    }
    if (auto* cached = cache_.Get()) {
        return cached;
    }
    void* resolved = nullptr;
    for (auto* p = *lod114d::imports::d2win::gpFirstControl; p != nullptr; p = p->pNext) {
        if (TypeOf(p) == type_ && BoundsOf(p) == bounds_) {
            resolved = p;
            break;
        }
    }
    cache_.Set(resolved);
    return resolved;
}

Control::operator bool() const {
    GameReadLock guard;
    return ResolvePtr() != nullptr;
}

Control Control::FromPtr(void* p) {
    if (p == nullptr) {
        return Control{};
    }
    auto* ctrl = AsCtrl(p);
    Control handle{TypeOf(ctrl), BoundsOf(ctrl)};
    handle.cache_.Set(p);
    return handle;
}

std::string Control::Text() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    // CONTROL_CLOAKED_PASSWORD (33) is the edit box flag set the game gives a
    // password field - reference reads it != 33 before exposing the text
    // (reference/d2bs/JSControl.cpp:81).
    if (!ctrl || AsEditBox(ctrl)->dwEditBoxFlags == CONTROL_CLOAKED_PASSWORD) {
        return {};
    }
    const Unicode* src = TypeOf(ctrl) == ControlType::Button ? AsButton(ctrl)->wszText : AsEditBox(ctrl)->wszText;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - D2MOO Unicode is wchar_t on Win32
    return utils::ToStr(std::wstring{reinterpret_cast<const wchar_t*>(src)});
}

void Control::SetText(const std::string& text) const {
    if (Type() != ControlType::EditBox) {
        return;
    }
    // Re-resolved on the game thread: a pointer resolved here goes stale while Execute waits.
    GameThread::Execute([self = *this, wide = utils::ToWStr(text)] {
        if (const auto ctrl = self.Resolve<D2WinControlStrc>(); ctrl && TypeOf(ctrl) == ControlType::EditBox) {
            lod114d::imports::d2win::EDITBOX_SetText(AsEditBox(ctrl), wide.c_str());
        }
    });
}

Rect Control::Bounds() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    return ctrl ? BoundsOf(ctrl) : Rect::Zero;
}

// The JS-visible "state" (this minus 2) and "disabled" (raw) views are derived in
// the bindings.
ControlState Control::State() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    return ctrl ? static_cast<ControlState>(ctrl->dwFlags) : ControlState{};
}

// Reference's setter memset's the field with a single repeated byte (buggy:
// fills 0x0D0D0D0D etc.); write the integer directly so State() round-trips.
void Control::SetState(ControlState value) const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    if (!ctrl) {
        return;
    }
    ctrl->dwFlags = std::to_underlying(value);
}

bool Control::IsPassword() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    return ctrl && AsEditBox(ctrl)->dwEditBoxFlags == CONTROL_CLOAKED_PASSWORD;
}

ControlType Control::Type() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    if (!ctrl) {
        return ControlType::Unknown;
    }
    return TypeOf(ctrl);
}

// Character-index caret, not a 2D screen coordinate.
uint32_t Control::CursorPos() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    return ctrl ? static_cast<uint32_t>(AsEditBox(ctrl)->nCaretPosition) : 0U;
}

// Same memset bug as SetState; see SetState comment.
void Control::SetCursorPos(uint32_t offset) const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    if (!ctrl) {
        return;
    }
    AsEditBox(ctrl)->nCaretPosition = static_cast<int32_t>(offset);
}

// Reference d2bs names 0x58 the selection start and 0x54 its end; the game
// keeps the anchor (-1 for none) at 0x54 and the caret end at 0x58. Scripts see
// the reference naming.
uint32_t Control::SelectStart() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    return ctrl ? static_cast<uint32_t>(AsEditBox(ctrl)->nSelectionEnd) : 0U;
}

uint32_t Control::SelectEnd() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    return ctrl ? static_cast<uint32_t>(AsEditBox(ctrl)->nSelectionStart) : 0U;
}

bool Control::HasLocaleText(uint16_t localeId) const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    if (!ctrl) {
        return false;
    }
    const auto* localeText = lod114d::imports::d2lang::D2LANG_GetLocaleText(localeId);
    if (localeText == nullptr) {
        return false;
    }
    // Reference findControl: match the resolved locale string against the button's
    // text (exact), or find it within a textbox's text.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast) - D2MOO Unicode is wchar_t on Win32
    if (TypeOf(ctrl) == ControlType::Button) {
        return std::wstring_view{localeText} ==
               std::wstring_view{reinterpret_cast<const wchar_t*>(AsButton(ctrl)->wszText)};
    }
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    if (TypeOf(ctrl) == ControlType::TextBox) {
        const auto* firstLine = AsTextBox(ctrl)->pFirstLine;
        if (firstLine == nullptr || firstLine->pColumns[0] == nullptr) {
            return false;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - D2MOO Unicode is wchar_t on Win32
        const auto* controlText = reinterpret_cast<const wchar_t*>(firstLine->pColumns[0]);
        return std::wstring_view{localeText}.contains(controlText);
    }
    return false;
}

Control Control::GetNext() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    if (!ctrl) {
        return Control{};
    }
    return FromPtr(ctrl->pNext);
}

void Control::Click(std::optional<Position> pos) const {
    if (GetGameState() != GameState::Menu) {
        return;
    }
    // Resolve the click position under a brief read lock. The PostMessages and
    // sleeps that follow are thread-safe (PostMessageW is) and intentionally run
    // *outside* any lock or GameThread::Execute - callers must invoke Click from a
    // non-game thread so the sleeps don't block the game's frame loop.
    uint32_t cx = 0U;
    uint32_t cy = 0U;
    {
        const auto ctrl = Resolve<D2WinControlStrc>();
        if (!ctrl) {
            return;
        }
        // D2 anchors a control's rect at its bottom-left: dwPosX is the left edge
        // but dwPosY is the *bottom* edge, so the clickable center is
        // (posX + sizeX/2, posY - sizeY/2). Reference clickControl does exactly this
        // (reference/d2bs/Control.cpp:118-121). Rect::Center() adds half the height
        // on both axes, which lands sizeY/2 too low and clicks the control stacked
        // directly below (e.g. Single Player hits Battle.net).
        const auto r = BoundsOf(ctrl);
        cx = (!pos || pos->x == ~0U) ? r.origin.x + (r.size.width / 2) : pos->x;
        cy = (!pos || pos->y == ~0U) ? r.origin.y - (r.size.height / 2) : pos->y;
    }

    PostMouseMessage(WM_LBUTTONDOWN, static_cast<int32_t>(cx), static_cast<int32_t>(cy));
    constexpr std::chrono::milliseconds CLICK_STEP_DELAY{100};
    std::this_thread::sleep_for(CLICK_STEP_DELAY);
    PostMouseMessage(WM_LBUTTONUP, static_cast<int32_t>(cx), static_cast<int32_t>(cy));
    // Let the game process the release before scripts inspect the next menu.
    // Without this pause, TCP-host login sees the old TCP/IP screen after
    // clicking Host and queues Cancel onto the new character-select screen.
    std::this_thread::sleep_for(CLICK_STEP_DELAY);
}

std::vector<Control::TextLine> Control::TextLines() const {
    const auto ctrl = Resolve<D2WinControlStrc>();
    if (!ctrl || TypeOf(ctrl) != ControlType::TextBox) {
        return {};
    }
    std::vector<TextLine> out;
    for (auto* node = AsTextBox(ctrl)->pFirstLine; node != nullptr; node = node->pNext) {
        if (node->pColumns[0] == nullptr) {
            continue;
        }
        TextLine line;
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by TEXT_SLOTS
        for (size_t i = 0; i < TEXT_SLOTS; ++i) {
            if (auto* slot = node->pColumns[i]; slot != nullptr) {
                // D2MOO's Unicode is binary-compatible with wchar_t on Win32 (both 16-bit).
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                line[i] = utils::ToStr(std::wstring{reinterpret_cast<const wchar_t*>(slot)});
            }
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
        out.push_back(std::move(line));
    }
    return out;
}

std::optional<Control> Control::GetFirst() {
    GameReadLock guard;
    auto* first = *lod114d::imports::d2win::gpFirstControl;
    if (first == nullptr) {
        return std::nullopt;
    }
    return FromPtr(first);
}

}  // namespace d2bs::game
