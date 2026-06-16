#include "game/Control.h"

#include <Windows.h>

#include <chrono>
#include <string_view>
#include <thread>

#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/GameThread.h"
#include "hooks/HookManager.h"
#include "imports/D2Gfx.h"
#include "imports/D2Lang.h"
#include "imports/D2Win.h"
#include "imports/extras/D2WinControlStrc.h"
#include "utils/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2WinButton.h>   // D2WinButtonStrc (nStringId access)
#include <D2WinTextBox.h>  // D2WinTextBoxLineStrc (full layout, for pColumns/pNext field access)
#pragma clang diagnostic pop

namespace d2bs::game {

using d2bs::imports::extras::D2WinControlStrc;

namespace {

constexpr std::chrono::milliseconds CLICK_STEP_DELAY{100};

D2WinControlStrc* AsCtrl(void* p) noexcept {
    return static_cast<D2WinControlStrc*>(p);
}

void PostMouseMessage(uint32_t msg, int32_t x, int32_t y) {
    auto* hwnd = imports::d2gfx::WINDOW_GetWindow();
    if (hwnd == nullptr) {
        return;
    }
    const LPARAM lp = (x & 0xFFFF) | ((y & 0xFFFF) << 16);
    hooks::PostInjectedInput(hwnd, msg, 0, lp);
}

}  // namespace

void* Control::ResolvePtr() const {
    GameReadLock guard;
    if (type_ == ControlType::Unknown && bounds_ == Rect::Zero) {
        return nullptr;
    }
    if (auto* cached = cache_.Get()) {
        return cached;
    }
    void* resolved = nullptr;
    for (auto* p = *imports::d2win::gpFirstControl; p != nullptr; p = p->pNext) {
        if (p->dwType == type_ && p->rect == bounds_) {
            resolved = p;
            break;
        }
    }
    cache_.Set(resolved);
    return resolved;
}

Control::operator bool() const {
    return ResolvePtr() != nullptr;
}

Control Control::FromPtr(void* p) {
    if (p == nullptr) {
        return Control{};
    }
    auto* ctrl = AsCtrl(p);
    return Control{ctrl->dwType, ctrl->rect};
}

std::string Control::Text() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    // CONTROL_CLOAKED_PASSWORD (33) is the dwIsCloaked sentinel the game uses
    // to mark a textbox as a password field - reference reads dwIsCloaked != 33
    // before exposing wText (reference/d2bs/JSControl.cpp:81).
    if (ctrl == nullptr || ctrl->dwIsCloaked == CONTROL_CLOAKED_PASSWORD) {
        return {};
    }
    const wchar_t* src = ctrl->dwType == ControlType::Button ? ctrl->wText2.data() : ctrl->wText.data();
    return utils::ToStr(std::wstring{src});
}

void Control::SetText(const std::string& text) const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr || ctrl->dwType != ControlType::EditBox) {
        return;
    }
    auto wide = utils::ToWStr(text);
    GameThread::Execute([ctrl, wide = std::move(wide)] { imports::d2win::CONTROL_SetText(ctrl, wide.c_str()); });
}

Rect Control::Bounds() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    return ctrl != nullptr ? ctrl->rect : Rect::Zero;
}

int32_t Control::State() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(ctrl->dwDisabled) - 2;
}

// Reference uses memset(&dwDisabled, value+2, sizeof(DWORD)) which fills 4 bytes
// with the byte (value+2) - this is a bug: dwDisabled gets 0x02020202 etc.
// instead of the integer value+2, breaking round-trips with State() which does
// `dwDisabled - 2`. State() reads dwDisabled as a full uint32, so the integer
// write below is the only encoding both Read and Write agree on for value in
// the documented JS-visible range (0..3 -> dwDisabled 2..5).
void Control::SetState(int32_t value) const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return;
    }
    ctrl->dwDisabled = static_cast<uint32_t>(value + 2);
}

bool Control::IsPassword() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    return ctrl != nullptr && ctrl->dwIsCloaked == CONTROL_CLOAKED_PASSWORD;
}

ControlType Control::Type() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return ControlType::Unknown;
    }
    return ctrl->dwType;
}

// Character-index caret, not a 2D screen coordinate.
uint32_t Control::CursorPos() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    return ctrl != nullptr ? ctrl->dwCursorPos : 0U;
}

// Same memset bug as SetState; see SetState comment.
void Control::SetCursorPos(uint32_t offset) const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return;
    }
    ctrl->dwCursorPos = offset;
}

uint32_t Control::SelectStart() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    return ctrl != nullptr ? ctrl->dwSelectStart : 0U;
}

uint32_t Control::SelectEnd() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    return ctrl != nullptr ? ctrl->dwSelectEnd : 0U;
}

// Multi-state sentinel field; 0x0D = difficulty button enabled, 0x04 = Bnet diff unavailable, etc. IsAvailable() gives
// the boolean view.
uint32_t Control::Disabled() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    return ctrl != nullptr ? ctrl->dwDisabled : 0U;
}

// Same memset bug as SetState; see SetState comment.
void Control::SetDisabled(uint32_t value) const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return;
    }
    ctrl->dwDisabled = value;
}

// `nStringId` is at offset 0x270 inside D2WinButtonStrc (D2Win/D2WinButton.h).
// The game allocates a D2WinButtonStrc-sized buffer for ControlType::Button,
// so reading past our 0x264-sized shadow into +0x270 is safe when dwType is
// Button. TextBox controls don't store a locale id - their text is the
// already-resolved wide string from D2LANG_GetLocaleText.
static_assert(offsetof(::D2WinButtonStrc, nStringId) == 0x270, "D2WinButtonStrc::nStringId offset drift vs D2MOO");

bool Control::HasLocaleText(int32_t localeId) const {
    if (localeId < 0) {
        return false;
    }
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return false;
    }
    if (ctrl->dwType == ControlType::Button) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const ::D2WinButtonStrc*>(ctrl)->nStringId == localeId;
    }
    if (ctrl->dwType == ControlType::TextBox) {
        const auto* localeText = imports::d2lang::D2LANG_GetLocaleText(static_cast<uint16_t>(localeId));
        if (localeText == nullptr || ctrl->pFirstText == nullptr || ctrl->pFirstText->pColumns[0] == nullptr) {
            return false;
        }
        // D2MOO's Unicode is binary-compatible with wchar_t on Win32 (both 16-bit).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto* controlText = reinterpret_cast<const wchar_t*>(ctrl->pFirstText->pColumns[0]);
        return std::wstring_view{localeText}.contains(controlText);
    }
    return false;
}

bool Control::IsAvailable() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return false;
    }
    return ctrl->dwType != ControlType::Button || ctrl->unkState != 1;
}

Control Control::GetNext() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr) {
        return Control{};
    }
    return Control::FromPtr(ctrl->pNext);
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
        GameReadLock guard;
        auto* ctrl = AsCtrl(ResolvePtr());
        if (ctrl == nullptr) {
            return;
        }
        // D2 anchors a control's rect at its bottom-left: dwPosX is the left edge
        // but dwPosY is the *bottom* edge, so the clickable center is
        // (posX + sizeX/2, posY - sizeY/2). Reference clickControl does exactly this
        // (reference/d2bs/Control.cpp:118-121). Rect::Center() adds half the height
        // on both axes, which lands sizeY/2 too low and clicks the control stacked
        // directly below (e.g. Single Player hits Battle.net).
        const auto& r = ctrl->rect;
        cx = (!pos || pos->x == ~0U) ? r.origin.x + (r.size.width / 2) : pos->x;
        cy = (!pos || pos->y == ~0U) ? r.origin.y - (r.size.height / 2) : pos->y;
    }

    PostMouseMessage(WM_LBUTTONDOWN, static_cast<int32_t>(cx), static_cast<int32_t>(cy));
    std::this_thread::sleep_for(CLICK_STEP_DELAY);
    PostMouseMessage(WM_LBUTTONUP, static_cast<int32_t>(cx), static_cast<int32_t>(cy));
}

std::vector<Control::TextLine> Control::TextLines() const {
    auto* ctrl = AsCtrl(ResolvePtr());
    if (ctrl == nullptr || ctrl->dwType != ControlType::TextBox) {
        return {};
    }
    std::vector<TextLine> out;
    for (auto* node = ctrl->pFirstText; node != nullptr; node = node->pNext) {
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
    auto* first = *imports::d2win::gpFirstControl;
    if (first == nullptr) {
        return std::nullopt;
    }
    return Control::FromPtr(first);
}

}  // namespace d2bs::game
