#pragma once

#include <utility>

#include <v8.h>
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/core/Extract.h"
#include "game/Control.h"
#include "game/GameHelpers.h"

namespace d2bs::runtime::api::classes {

// Control class - represents a UI control element in menu screens
// Controls are interactive elements like buttons, text boxes, labels
class JSControl : public ClassBase<JSControl, game::Control> {
   public:
    static constexpr std::string_view ClassName = "Control";

    // Control objects are obtained via getControl() factory function, not constructable
    V8_CLASS_NOT_CONSTRUCTABLE

   private:
    // The JS `state` view is the raw state word counted from ControlState::Normal.
    static constexpr int32_t STATE_BASE = std::to_underlying(game::ControlState::Normal);

    // Shared guard for every Control property callback: the client must be in the
    // Menu state AND the underlying Control handle must still resolve to a valid
    // game control. Returns the locked Control when both hold, else an empty one
    // (caller should early-return). Both checks and the caller's reads run under
    // the one lock the result holds.
    //
    // Works for property getters (PropertyCallbackInfo<v8::Value>) and property
    // setters (PropertyCallbackInfo<Boolean>).
    template <typename InfoT>
    static LockedHandle<game::Control> MenuOnly(const InfoT& info) {
        auto data = Unwrap(info.Holder());
        if (game::GetGameState() != game::GameState::Menu || !data || !*data) {
            return LockedHandle<game::Control>(nullptr);
        }
        return data;
    }

   public:
    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        // Properties
        /// @description The control's text content (undefined for password fields); assigning sets EditBox text.
        /// @type {string}
        Property(
            isolate, inst, "text",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                // Text can wait on the game thread, so it is read from a copy after the lock is dropped.
                game::Control control;
                {
                    const auto data = MenuOnly(info);
                    if (!data)
                        return;
                    // Reference: skip setting return value for password fields (dwIsCloaked==33)
                    // so JS sees undefined, matching reference behavior
                    if (data->IsPassword()) {
                        return;
                    }
                    control = *data;
                }
                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), control.Text()));
            },
            +[](v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                // SetText waits on the game thread, so it runs on a copy after the lock is dropped.
                game::Control control;
                {
                    const auto data = MenuOnly(info);
                    if (!data)
                        return;
                    // Only editable text controls (EditBox) support SetText
                    if (data->Type() != game::ControlType::EditBox || !value->IsString()) {
                        return;
                    }
                    control = *data;
                }
                control.SetText(convert::ToString(info.GetIsolate(), value));
            });
        /// @description The control's left (x) screen coordinate.
        /// @type {number}
        Property(
            isolate, inst, "x", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().origin.x));
            });
        /// @description The control's top (y) screen coordinate.
        /// @type {number}
        Property(
            isolate, inst, "y", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().origin.y));
            });
        /// @description The control's width in pixels.
        /// @type {number}
        Property(
            isolate, inst, "xsize", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().size.width));
            });
        /// @description The control's height in pixels.
        /// @type {number}
        Property(
            isolate, inst, "ysize", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->Bounds().size.height));
            });
        /// @description The control's state, 0-3: the raw state word minus 2 (0 normal, 2 a disabled button, 3 an
        /// active edit box or ticked checkbox).
        /// @type {number}
        /// @throws {Error} - when the assigned state is outside the 0-3 range
        Property(
            isolate, inst, "state",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(std::to_underlying(data->State())) - STATE_BASE);
            },
            +[](v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                auto* isolate = info.GetIsolate();
                if (!value->IsNumber()) {
                    return;
                }
                int32_t state = convert::ToInt32(isolate, value);
                if (state < 0 || state > 3) {
                    error::ThrowError(isolate, "Invalid state value");
                    return;
                }
                data->SetState(static_cast<game::ControlState>(state + STATE_BASE));
            });
        /// @description Whether the control is a password (cloaked) input field.
        /// @type {boolean}
        Property(
            isolate, inst, "password",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(data->IsPassword());
            });
        /// @description The control's type as a numeric ControlType code (button, EditBox, TextBox, etc).
        /// 1 = edit box, 2 = image, 4 = text box, 5 = scroll bar, 6 = button, 7 = list (0 = unknown, 3 = unused).
        /// @type {number}
        Property(
            isolate, inst, "type", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<uint32_t>(data->Type()));
            });
        /// @description The text-caret character offset within the control's text buffer.
        /// @type {number}
        Property(
            isolate, inst, "cursorpos",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->CursorPos()));
            },
            +[](v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                if (!value->IsNumber()) {
                    error::ThrowError(info.GetIsolate(), "Invalid cursor position value");
                    return;
                }
                auto* isolate = info.GetIsolate();
                uint32_t pos = convert::ToUint32(isolate, value);
                data->SetCursorPos(pos);
            });
        /// @description The start character offset of the control's current text selection.
        /// @type {number}
        Property(
            isolate, inst, "selectstart",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->SelectStart()));
            });
        /// @description The end character offset of the control's current text selection.
        /// @type {number}
        Property(
            isolate, inst, "selectend",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(static_cast<int32_t>(data->SelectEnd()));
            });
        /// @description The control's raw state word; ControlState names the known values.
        /// @type {ControlState|number}
        Property(
            isolate, inst, "disabled",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                info.GetReturnValue().Set(std::to_underlying(data->State()));
            },
            +[](v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                const auto data = MenuOnly(info);
                if (!data)
                    return;
                if (!value->IsNumber()) {
                    return;
                }
                auto* isolate = info.GetIsolate();
                data->SetState(static_cast<game::ControlState>(convert::ToUint32(isolate, value)));
            });

        // Methods
        /// @description Advances this Control handle in place to the next control in the menu's control list.
        /// @signature getNext()
        /// @returns {Control|boolean} - this Control advanced to the next control, or false if there is none / the
        /// handle is stale
        Method(
            isolate, proto, "getNext", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                const auto data = Unwrap(args.This());
                if (game::GetGameState() != game::GameState::Menu) {
                    return;
                }
                if (!data || !*data) {
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
            isolate, proto, "click", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                // Click waits on the game thread, so it runs on a copy after the lock is dropped.
                game::Control control;
                {
                    const auto data = Unwrap(args.This());
                    if (game::GetGameState() != game::GameState::Menu) {
                        return;
                    }
                    if (!data || !*data) {
                        args.GetReturnValue().Set(0);  // Reference returns 0 on stale control
                        return;
                    }
                    control = *data;
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
                if (args.Length() > 1 && args[0]->IsInt32() && args[1]->IsInt32()) {
                    pos = extract::Position(args, 0);
                }
                control.Click(pos);
            });
        /// @description Sets the control's text to the given string.
        /// @signature setText(text: string)
        /// @param text {string} - the new text to set
        /// @returns {undefined|number} - undefined normally; 0 if the control handle is stale
        Method(
            isolate, proto, "setText", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                // SetText waits on the game thread, so it runs on a copy after the lock is dropped.
                game::Control control;
                {
                    const auto data = Unwrap(args.This());
                    if (game::GetGameState() != game::GameState::Menu) {
                        return;
                    }
                    if (!data || !*data) {
                        args.GetReturnValue().Set(0);  // Reference: INT_TO_JSVAL(0) on stale control
                        return;
                    }
                    control = *data;
                }
                if (args.Length() < 1 || !args[0]->IsString()) {
                    return;  // Silent return, matching reference
                }
                control.SetText(convert::ToString(args.GetIsolate(), args[0]));
            });
        /// @description Returns the text lines of a list control (TextBox); undefined for other control types.
        /// @signature getText()
        /// @returns {Array<string|Array<string>>} - text lines; each is a string (single slot) or a sparse array
        /// (multiple slots); 0 if the control handle is stale
        Method(
            isolate, proto, "getText", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                // TextLines can wait on the game thread, so it is read from a copy after the lock is dropped.
                game::Control control;
                {
                    const auto data = Unwrap(args.This());
                    if (game::GetGameState() != game::GameState::Menu) {
                        return;
                    }
                    if (!data || !*data) {
                        args.GetReturnValue().Set(0);  // Reference: INT_TO_JSVAL(0) on stale control
                        return;
                    }
                    // Only list controls (TextBox) have text lines
                    if (data->Type() != game::ControlType::TextBox) {
                        return;
                    }
                    control = *data;
                }
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();

                auto lines = control.TextLines();
                auto array = v8::Array::New(isolate, static_cast<int32_t>(lines.size()));

                for (size_t i = 0; i < lines.size(); ++i) {
                    const auto& line = lines[i];
                    v8::Local<v8::Value> value;
                    // Reference: check wText[1] to decide single-string vs sub-array
                    if (line[1].has_value()) {
                        // Multiple text slots: return as sparse sub-array preserving slot indices
                        auto inner = v8::Array::New(isolate);
                        for (uint32_t j = 0; j < game::Control::TEXT_SLOTS; ++j) {
                            if (line.at(j).has_value()) {
                                inner->Set(context, j, convert::ToJS(isolate, *line.at(j))).Check();
                            }
                        }
                        value = inner;
                    } else if (line[0].has_value()) {
                        // Single text entry (slot 0 only): return as string directly
                        value = convert::ToJS(isolate, *line[0]);
                    }
                    if (!value.IsEmpty()) {
                        array->Set(context, i, value).Check();
                    }
                }

                args.GetReturnValue().Set(array);
            });
    }
};

}  // namespace d2bs::runtime::api::classes
