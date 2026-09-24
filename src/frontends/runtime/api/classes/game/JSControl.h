#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/core/Extract.h"
#include "game/Control.h"
#include "game/GameHelpers.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Control class - represents a UI control element in menu screens
// Controls are interactive elements like buttons, text boxes, labels
class JSControl : public ClassBase<JSControl, game::Control> {
   public:
    static constexpr std::string_view ClassName = "Control";

   private:
    // Shared guard for every Control property callback: the client must be in the Menu state AND
    // the receiver must be a Control whose handle still resolves to a valid game control. Returns
    // the unwrapped Control* when both hold, else nullptr (caller should early-return).
    static game::Control* MenuOnly(const ub::PropertyCallbackInfo& info) {
        if (game::GetGameState() != game::GameState::Menu) {
            return nullptr;
        }
        auto* data = Unwrap(info.This());
        if (data == nullptr || !*data) {
            return nullptr;
        }
        return data;
    }

   public:
    static void Configure(const ub::Class<Native>& cls) {
        // Properties
        /// @description The control's text content (undefined for password fields); assigning sets EditBox text.
        /// @type {string}
        Property(
            cls, "text",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                // Reference: skip setting return value for password fields (dwIsCloaked==33)
                // so JS sees undefined, matching reference behavior
                if (data->IsPassword()) {
                    return;
                }
                std::ignore = info.GetReturnValue().Set(data->Text());
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                // Only editable text controls (EditBox) support SetText
                if (data->Type() != game::ControlType::EditBox || !value.IsString()) {
                    return;
                }
                data->SetText(convert::ToString(info.GetContext(), value));
            });
        /// @description The control's left (x) screen coordinate.
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().origin.x));
            });
        /// @description The control's top (y) screen coordinate.
        /// @type {number}
        Property(
            cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().origin.y));
            });
        /// @description The control's width in pixels.
        /// @type {number}
        Property(
            cls, "xsize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().size.width));
            });
        /// @description The control's height in pixels.
        /// @type {number}
        Property(
            cls, "ysize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().size.height));
            });
        /// @description The control's state value (0-3).
        /// @type {number}
        /// @throws {Error} - when the assigned state is outside the 0-3 range
        Property(
            cls, "state",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->State()) - 2);
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                if (!value.IsNumber()) {
                    return;
                }
                int32_t state = convert::ToInt32(info.GetContext(), value);
                if (state < 0 || state > 3) {
                    error::ThrowError(info.GetIsolate(), "Invalid state value");
                    return;
                }
                data->SetState(static_cast<uint32_t>(state + 2));
            });
        /// @description Whether the control is a password (cloaked) input field.
        /// @type {boolean}
        Property(
            cls, "password", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(data->IsPassword());
            });
        /// @description The control's type as a numeric ControlType code (button, EditBox, TextBox, etc).
        /// 1 = edit box, 2 = image, 4 = text box, 5 = scroll bar, 6 = button, 7 = list (0 = unknown, 3 = unused).
        /// @type {number}
        Property(
            cls, "type", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<uint32_t>(data->Type()));
            });
        /// @description The text-caret character offset within the control's text buffer.
        /// @type {number}
        Property(
            cls, "cursorpos",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->CursorPos()));
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                if (!value.IsNumber()) {
                    error::ThrowError(info.GetIsolate(), "Invalid cursor position value");
                    return;
                }
                uint32_t pos = convert::ToUint32(info.GetContext(), value);
                data->SetCursorPos(pos);
            });
        /// @description The start character offset of the control's current text selection.
        /// @type {number}
        Property(
            cls, "selectstart", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->SelectStart()));
            });
        /// @description The end character offset of the control's current text selection.
        /// @type {number}
        Property(
            cls, "selectend", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->SelectEnd()));
            });
        /// @description The control's disabled flag value.
        /// @type {number}
        Property(
            cls, "disabled",
            +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->State()));
            },
            +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
                auto* data = MenuOnly(info);
                if (!data)
                    return;
                if (!value.IsNumber()) {
                    return;
                }
                uint32_t disabled = convert::ToUint32(info.GetContext(), value);
                data->SetState(disabled);
            });

        // Methods
        /// @description Advances this Control handle in place to the next control in the menu's control list.
        /// @signature getNext()
        /// @returns {Control|boolean} - this Control advanced to the next control, or false if there is none / the
        /// handle is stale
        Method(
            cls, "getNext", +[](const ub::CallbackInfo& args) {
                if (game::GetGameState() != game::GameState::Menu) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (data == nullptr || !*data) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                auto next = data->GetNext();
                if (!next) {
                    args.GetReturnValue().SetFalse();
                    return;
                }
                *data = next;
                args.GetReturnValue().Set(args.This());
            });
        /// @description Clicks the control, at the given screen coordinate or its default center point.
        /// @signature click()
        /// @signature click(x: number, y: number)
        /// @param x {number} - click x screen coordinate; -1 uses the control's default x
        /// @param y {number} - click y screen coordinate; -1 uses the control's default y
        /// @returns {undefined|number} - undefined normally; 0 if the control handle is stale
        Method(
            cls, "click", +[](const ub::CallbackInfo& args) {
                if (game::GetGameState() != game::GameState::Menu) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (data == nullptr || !*data) {
                    args.GetReturnValue().Set(0);  // Reference returns 0 on stale control
                    return;
                }
                // Reference control_click (reference/d2bs/JSControl.cpp:202-209) seeds x/y
                // as (uint32)-1, then overwrites both iff argc>1 and both args are ints
                // (JSVAL_IS_INT, which accepts negatives). Negatives flow through as
                // UINT32_MAX via ECMAScript ToUint32; the game-layer Control::Click treats
                // UINT32_MAX per-axis as "use control default", matching reference
                // clickControl (reference/d2bs/Control.cpp:118-121) where
                // `if (x == -1) x = dwPosX + dwSizeX / 2`. Preserves the asymmetric
                // `click(-1, 400)` = "default X, Y=400" case that a uint-only gate would
                // collapse into "default both".
                std::optional<game::Position> pos;
                if (args.Length() > 1 && args[0].IsInt32() && args[1].IsInt32()) {
                    pos = extract::Position(args, 0);
                }
                data->Click(pos);
            });
        /// @description Sets the control's text to the given string.
        /// @signature setText(text: string)
        /// @param text {string} - the new text to set
        /// @returns {undefined|number} - undefined normally; 0 if the control handle is stale
        Method(
            cls, "setText", +[](const ub::CallbackInfo& args) {
                if (game::GetGameState() != game::GameState::Menu) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (data == nullptr || !*data) {
                    args.GetReturnValue().Set(0);  // Reference: INT_TO_JSVAL(0) on stale control
                    return;
                }
                if (args.Length() < 1 || !args[0].IsString()) {
                    return;  // Silent return, matching reference
                }
                data->SetText(convert::ToString(args.GetContext(), args[0]));
            });
        /// @description Returns the text lines of a list control (TextBox); undefined for other control types.
        /// @signature getText()
        /// @returns {Array<string|Array<string>>} - text lines; each is a string (single slot) or a sparse array
        /// (multiple slots); 0 if the control handle is stale
        Method(
            cls, "getText", +[](const ub::CallbackInfo& args) {
                if (game::GetGameState() != game::GameState::Menu) {
                    return;
                }
                auto* data = Unwrap(args.This());
                if (data == nullptr || !*data) {
                    args.GetReturnValue().Set(0);  // Reference: INT_TO_JSVAL(0) on stale control
                    return;
                }
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();

                // Only list controls (TextBox) have text lines
                if (data->Type() != game::ControlType::TextBox) {
                    return;
                }

                auto lines = data->TextLines();
                auto array = ub::Array::New(context, static_cast<uint32_t>(lines.size()));
                if (!array) {
                    return;
                }

                for (uint32_t i = 0; i < lines.size(); ++i) {
                    const auto& line = lines[i];
                    ub::Local<ub::Value> value;
                    // Reference: check wText[1] to decide single-string vs sub-array
                    if (line[1].has_value()) {
                        // Multiple text slots: return as sparse sub-array preserving slot indices
                        auto inner = ub::Array::New(context);
                        if (!inner) {
                            return;
                        }
                        for (uint32_t j = 0; j < game::Control::TEXT_SLOTS; ++j) {
                            if (line.at(j).has_value() &&
                                !inner->Set(context, j, convert::ToJS(isolate, *line.at(j))).value_or(false)) {
                                return;
                            }
                        }
                        value = *inner;
                    } else if (line[0].has_value()) {
                        // Single text entry (slot 0 only): return as string directly
                        value = convert::ToJS(isolate, *line[0]);
                    }
                    if (!value.IsEmpty() && !array->Set(context, i, value).value_or(false)) {
                        return;
                    }
                }

                args.GetReturnValue().Set(*array);
            });
    }
};

}  // namespace d2bs::api::classes
