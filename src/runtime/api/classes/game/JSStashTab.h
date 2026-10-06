#pragma once

#include <v8.h>

#include <memory>
#include <optional>

#include "api/classes/game/JSUnit.h"
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"
#include "game/GameHelpers.h"
#include "game/StashTab.h"
#include "game/Unit.h"

namespace d2bs::runtime::api::classes {

// V8 binding for game::StashTab (docs/plugy_stash.md). Obtained from getStashTabs()
// and Unit.stashTab; never constructed by scripts.
class JSStashTab : public ClassBase<JSStashTab, game::StashTab> {
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
                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), Unwrap(info.Holder())->Name()));
            });

        /// @description Gold stored on the tab. Where the game keeps one gold figure per stash rather than per tab,
        /// it is attributed to the first tab of that kind and the other tabs read 0.
        /// @type {number}
        Property(
            isolate, inst, "gold", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                info.GetReturnValue().Set(Unwrap(info.Holder())->Gold());
            });

        /// @description The tab's items, whether or not the tab is the one shown. Items on tabs that are not shown
        /// are live units and behave like any other Unit. Built on each access; empty for a tab that is gone.
        /// @type {Unit[]}
        Property(
            isolate, inst, "items", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto context = isolate->GetCurrentContext();
                const auto items = Unwrap(info.Holder())->GetItems();
                auto arr = v8::Array::New(isolate, static_cast<int32_t>(items.size()));
                uint32_t i = 0;
                for (const auto& item : items) {
                    auto obj = JSUnit::CreateInstance(isolate, context, std::make_unique<game::Unit>(item));
                    if (obj.IsEmpty()) {
                        error::ThrowError(isolate, "Failed to build item array");
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
        /// item)` reaches items on such tabs the same way. An advanced stash tab has no grid to drop on: with
        /// an item on the cursor the click deposits it and the cell is ignored, otherwise it takes one item of the
        /// stack at the cell to the cursor - `deposit(item)` and `withdraw(item)` with the defaults.
        /// @signature click(x: number, y: number)
        /// @param x {number} - stash grid column
        /// @param y {number} - stash grid row
        /// @returns {boolean} - true if the click was handed to the game; false for a tab that is gone or could not
        /// be brought in, an open trade, nothing an advanced tab can do at the cell, or not being in a game. As with
        /// clickItem, true does not confirm the item moved.
        /// @signature click(item: Unit)
        /// @param item {Unit} - an item on this tab; clicks the cell it sits on
        /// @returns {boolean} - as for click(x, y); also false for an item that is not on this tab
        Method(
            isolate, proto, "click", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                const bool byItem = args.Length() >= 1 && JSUnit::IsInstance(args[0]);
                if (!byItem && (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber())) {
                    error::ThrowTypeError(isolate, "StashTab.click expects (x, y) or an item");
                    return;
                }
                if (!game::WaitForGameReady(core::config::GetAppConfig().gameReadyTimeout)) {
                    error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                // Click can block while the tab is brought in, so it runs on a copy with no lock held.
                const game::StashTab tab = *Unwrap(args.This());
                game::Position cell;
                if (byItem) {
                    game::Unit item;
                    {
                        const auto data = JSUnit::Unwrap(args[0].As<v8::Object>());
                        if (!data || !*data) {
                            args.GetReturnValue().SetFalse();
                            return;
                        }
                        item = *data;
                    }
                    const auto onTab = item.StashTab();
                    if (!onTab || onTab->Kind() != tab.Kind() || onTab->Index() != tab.Index()) {
                        args.GetReturnValue().SetFalse();
                        return;
                    }
                    cell = item.Pos();
                } else {
                    cell = {.x = convert::To<uint32_t>(isolate, args[0]), .y = convert::To<uint32_t>(isolate, args[1])};
                }
                args.GetReturnValue().Set(tab.Click(cell) == game::ClickResult::Dispatched);
            });

        /// @description Takes items off this tab. On an advanced stash tab `item` is a stack: any count goes to any
        /// target, and the server stops early when the stack runs out or the target has no room. On a 1.14d tab only
        /// one item to the cursor works - it picks the item up, with nothing else on the cursor. The stash panel must
        /// be open. Fire and forget: `itemcount` and the item events update when the server answers.
        /// @signature withdraw(item: Unit, count?: number, target?: StashWithdrawTarget)
        /// @param item {Unit} - an item on this tab, from `items`
        /// @param count {number} - how many to take; defaults to 1
        /// @param target {StashWithdrawTarget} - where to put them; defaults to the cursor, which takes only 1
        /// @returns {boolean} - true once the request was issued; false for an item that is not on this tab, a count
        /// of 0, what the tab cannot do (see above), a closed stash panel, or not being in a game
        /// @throws {TypeError} - a count or target that is not a number
        /// @throws {RangeError} - a target that is not a StashWithdrawTarget, or a count above 1 to the cursor
        Method(
            isolate, proto, "withdraw", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                if (args.Length() < 1 || !JSUnit::IsInstance(args[0])) {
                    error::ThrowTypeError(isolate, "StashTab.withdraw expects an item");
                    return;
                }
                uint32_t count = 1;
                if (args.Length() >= 2 && !args[1]->IsUndefined()) {
                    if (!args[1]->IsNumber()) {
                        error::ThrowTypeError(isolate, "StashTab.withdraw count must be a number");
                        return;
                    }
                    count = convert::To<uint32_t>(isolate, args[1]);
                }
                auto target = game::StashWithdrawTarget::Cursor;
                if (args.Length() >= 3 && !args[2]->IsUndefined()) {
                    if (!args[2]->IsNumber()) {
                        error::ThrowTypeError(isolate, "StashTab.withdraw target must be a StashWithdrawTarget");
                        return;
                    }
                    target = convert::To<game::StashWithdrawTarget>(isolate, args[2]);
                    if (target > game::StashWithdrawTarget::Belt) {
                        error::ThrowRangeError(isolate, "StashTab.withdraw target must be a StashWithdrawTarget");
                        return;
                    }
                }
                if (count > 1 && target == game::StashWithdrawTarget::Cursor) {
                    error::ThrowRangeError(isolate, "StashTab.withdraw takes only 1 to the cursor");
                    return;
                }
                if (!game::WaitForGameReady(core::config::GetAppConfig().gameReadyTimeout)) {
                    error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                game::Unit item;
                {
                    const auto data = JSUnit::Unwrap(args[0].As<v8::Object>());
                    if (!data || !*data) {
                        args.GetReturnValue().SetFalse();
                        return;
                    }
                    item = *data;
                }
                const game::StashTab tab = *Unwrap(args.This());
                args.GetReturnValue().Set(tab.Withdraw(item, count, target));
            });

        /// @description Puts an item on this tab's stack of its kind (advanced stash tabs only), the same as
        /// dropping it on the tab; the item may be on the cursor or anywhere you carry it. The stash panel must be
        /// open. Fire and forget: the server refuses a stack that already holds 99.
        /// @signature deposit(item: Unit)
        /// @param item {Unit} - an item you hold whose kind the tab stacks
        /// @returns {boolean} - true once the request was issued; false on any other kind of tab, for an item the tab
        /// does not stack or you do not hold, a closed stash panel, or not being in a game
        Method(
            isolate, proto, "deposit", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                if (args.Length() < 1 || !JSUnit::IsInstance(args[0])) {
                    error::ThrowTypeError(args.GetIsolate(), "StashTab.deposit expects an item");
                    return;
                }
                if (!game::WaitForGameReady(core::config::GetAppConfig().gameReadyTimeout)) {
                    error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                game::Unit item;
                {
                    const auto data = JSUnit::Unwrap(args[0].As<v8::Object>());
                    if (!data || !*data) {
                        args.GetReturnValue().SetFalse();
                        return;
                    }
                    item = *data;
                }
                const game::StashTab tab = *Unwrap(args.This());
                args.GetReturnValue().Set(tab.Deposit(item));
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
                    // MoveGold waits on the game thread, so it runs on a copy with no lock held.
                    const game::StashTab tab = *Unwrap(args.This());
                    args.GetReturnValue().Set(tab.MoveGold(game::GoldActionMode::Deposit, *amount));
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
                    // MoveGold waits on the game thread, so it runs on a copy with no lock held.
                    const game::StashTab tab = *Unwrap(args.This());
                    args.GetReturnValue().Set(tab.MoveGold(game::GoldActionMode::Withdraw, *amount));
                }
            });
    }

   private:
    // The single amount argument of the gold moves; nullopt (with the error or warning already
    // raised) when the call cannot proceed.
    static std::optional<uint32_t> GoldAmount(const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto* isolate = args.GetIsolate();
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            error::ThrowTypeError(isolate, "StashTab gold moves expect an amount");
            return std::nullopt;
        }
        if (!game::WaitForGameReady(core::config::GetAppConfig().gameReadyTimeout)) {
            error::WarnAndReturnFalse(args, "Game not ready");
            return std::nullopt;
        }
        return convert::To<uint32_t>(isolate, args[0]);
    }
};

}  // namespace d2bs::runtime::api::classes
