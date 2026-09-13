#pragma once

#include <v8.h>

#include <memory>
#include <optional>

#include "api/classes/game/JSUnit.h"
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "config/AppConfig.h"
#include "game/Bridge.h"
#include "game/GameHelpers.h"
#include "game/StashTab.h"
#include "game/Unit.h"

namespace d2bs::api::classes {

// V8 binding for game::StashTab (docs/plugy_stash.md). Obtained from getStashTabs()
// and Unit.stashTab; never constructed by scripts.
class JSStashTab : public V8ClassBase<JSStashTab, game::StashTab> {
   public:
    static constexpr std::string_view ClassName = "StashTab";

    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        /// @description Which stash the tab belongs to.
        /// @type {StashTabKind}
        Property(
            isolate, inst, "kind", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(static_cast<uint32_t>(Unwrap(info.Holder())->Kind()));
            });

        /// @description 0-based position of the tab within its kind.
        /// @type {number}
        Property(
            isolate, inst, "index", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(Unwrap(info.Holder())->Index());
            });

        /// @description What the tab holds; every LoD tab is StashTabType.normal.
        /// @type {StashTabType}
        Property(
            isolate, inst, "type", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(static_cast<uint32_t>(Unwrap(info.Holder())->Type()));
            });

        /// @description User-given tab name; empty when unnamed or when the tab is gone.
        /// @type {string}
        Property(
            isolate, inst, "name", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto lock = game::Bridge::Lock();
                info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), Unwrap(info.Holder())->Name()));
            });

        /// @description Gold stored on the tab. Where the game keeps one gold figure per stash rather than per tab,
        /// it is attributed to the first tab of that kind and the other tabs read 0.
        /// @type {number}
        Property(
            isolate, inst, "gold", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto lock = game::Bridge::Lock();
                info.GetReturnValue().Set(Unwrap(info.Holder())->Gold());
            });

        /// @description The tab's items, whether or not the tab is the one shown. Items on tabs that are not shown
        /// are live units and behave like any other Unit. Built on each access; empty for a tab that is gone.
        /// @type {Unit[]}
        Property(
            isolate, inst, "items", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto lock = game::Bridge::Lock();
                const auto items = Unwrap(info.Holder())->GetItems();
                auto arr = v8::Array::New(isolate, static_cast<int32_t>(items.size()));
                uint32_t i = 0;
                for (const auto& item : items) {
                    auto obj = JSUnit::CreateInstance(isolate, context, std::make_unique<game::Unit>(item));
                    if (obj.IsEmpty()) {
                        v8_error::ThrowError(isolate, "Failed to build item array");
                        return;
                    }
                    arr->Set(context, i++, obj).Check();
                }
                info.GetReturnValue().Set(arr);
            });

        /// @description Left-clicks a grid cell of this tab: picks up the item there, drops the cursor item, or
        /// swaps the two, as `clickItem(0, x, y, 7)` does on the tab that is shown. The stash panel must be open.
        /// Where the game cannot address a tab that is not shown, the call blocks while the tab is brought in,
        /// clicked, and the previous one restored, so the game state is consistent when it returns. `clickItem(0,
        /// item)` reaches items on such tabs the same way.
        /// @signature click(x: number, y: number)
        /// @param x {number} - stash grid column
        /// @param y {number} - stash grid row
        /// @returns {boolean} - true if the click was handed to the game; false for a tab that is gone or could not
        /// be brought in, an open trade, or not being in a game. As with clickItem, true does not confirm the item
        /// moved.
        Method(
            isolate, proto, "click", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
                    v8_error::ThrowTypeError(isolate, "StashTab.click(x, y) expects two numbers");
                    return;
                }
                if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                    v8_error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                const game::Position cell{.x = v8_convert::ToUint32(isolate, args[0]),
                                          .y = v8_convert::ToUint32(isolate, args[1])};
                args.GetReturnValue().Set(Unwrap(args.This())->Click(cell) == game::ClickResult::Dispatched);
            });

        /// @description Deposits carried gold into this tab. The stash panel must be open. Fire and forget like
        /// `gold()`: `gold` and your gold stats update when the server answers, so poll for the change afterwards.
        /// A shared pool that only supports moving everything ignores `amount`.
        /// @signature depositGold(amount: number)
        /// @param amount {number} - gold to deposit; ignored where the tab only supports moving everything
        /// @returns {boolean} - true once the move was requested; false for a tab that holds no gold, nothing to
        /// move, or not being in a game
        Method(
            isolate, proto, "depositGold", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                if (const auto amount = GoldAmount(args)) {
                    args.GetReturnValue().Set(Unwrap(args.This())->DepositGold(*amount));
                }
            });

        /// @description Withdraws gold from this tab into your carried gold. The stash panel must be open. Fire and
        /// forget like `gold()`: `gold` and your gold stats update when the server answers, so poll for the change
        /// afterwards. A shared pool that only supports moving everything ignores `amount`.
        /// @signature withdrawGold(amount: number)
        /// @param amount {number} - gold to withdraw; ignored where the tab only supports moving everything
        /// @returns {boolean} - true once the move was requested; false for a tab that holds no gold, nothing to
        /// move, or not being in a game
        Method(
            isolate, proto, "withdrawGold", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                if (const auto amount = GoldAmount(args)) {
                    args.GetReturnValue().Set(Unwrap(args.This())->WithdrawGold(*amount));
                }
            });
    }

   private:
    // The single amount argument of the gold moves; nullopt (with the error or warning already
    // raised) when the call cannot proceed.
    static std::optional<uint32_t> GoldAmount(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto* isolate = args.GetIsolate();
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            v8_error::ThrowTypeError(isolate, "StashTab gold moves expect an amount");
            return std::nullopt;
        }
        if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
            v8_error::WarnAndReturnFalse(args, "Game not ready");
            return std::nullopt;
        }
        return v8_convert::ToUint32(isolate, args[0]);
    }
};

}  // namespace d2bs::api::classes
