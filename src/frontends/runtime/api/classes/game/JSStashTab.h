#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>

#include "Receiver.h"
#include "api/classes/game/JSUnit.h"
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"
#include "game/Bridge.h"
#include "game/GameHelpers.h"
#include "game/StashTab.h"
#include "game/Unit.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Binding for game::StashTab (docs/plugy_stash.md). Obtained from getStashTabs()
// and Unit.stashTab; never constructed by scripts.
class JSStashTab : public ClassBase<JSStashTab, game::StashTab> {
   public:
    static constexpr std::string_view ClassName = "StashTab";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description Which stash the tab belongs to.
        /// @type {StashTabKind}
        Property(
            cls, "kind", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSStashTab>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<uint32_t>(data->Kind()));
            });

        /// @description 0-based position of the tab within its kind.
        /// @type {number}
        Property(
            cls, "index", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSStashTab>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(data->Index());
            });

        /// @description What the tab holds; every LoD tab is StashTabType.normal.
        /// @type {StashTabType}
        Property(
            cls, "type", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSStashTab>(info);
                if (data == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<uint32_t>(data->Type()));
            });

        /// @description User-given tab name; empty when unnamed or when the tab is gone.
        /// @type {string}
        Property(
            cls, "name", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSStashTab>(info);
                if (data == nullptr) {
                    return;
                }
                auto lock = game::Bridge::Lock();
                std::ignore = info.GetReturnValue().Set(data->Name());
            });

        /// @description Gold stored on the tab. Where the game keeps one gold figure per stash rather than per tab,
        /// it is attributed to the first tab of that kind and the other tabs read 0.
        /// @type {number}
        Property(
            cls, "gold", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSStashTab>(info);
                if (data == nullptr) {
                    return;
                }
                auto lock = game::Bridge::Lock();
                info.GetReturnValue().Set(data->Gold());
            });

        /// @description The tab's items, whether or not the tab is the one shown. Items on tabs that are not shown
        /// are live units and behave like any other Unit. Built on each access; empty for a tab that is gone.
        /// @type {Unit[]}
        Property(
            cls, "items", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSStashTab>(info);
                if (data == nullptr) {
                    return;
                }
                const auto& context = info.GetContext();
                auto lock = game::Bridge::Lock();
                const auto items = data->GetItems();
                auto arr = ub::Array::New(context, static_cast<uint32_t>(items.size()));
                if (!arr) {
                    return;
                }
                uint32_t i = 0;
                for (const auto& item : items) {
                    auto obj = JSUnit::Wrap(context, std::make_shared<game::Unit>(item));
                    if (!obj) {
                        error::ThrowError(info.GetIsolate(), "Failed to build item array");
                        return;
                    }
                    if (!arr->Set(context, i++, *obj).value_or(false)) {
                        return;
                    }
                }
                info.GetReturnValue().Set(*arr);
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
            cls, "click", +[](const ub::CallbackInfo& args) {
                auto* data = Receiver<JSStashTab>(args);
                if (data == nullptr) {
                    return;
                }
                if (args.Length() < 2 || !args[0].IsNumber() || !args[1].IsNumber()) {
                    error::ThrowTypeError(args.GetIsolate(), "StashTab.click(x, y) expects two numbers");
                    return;
                }
                if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                    error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                const auto& context = args.GetContext();
                const game::Position cell{.x = convert::ToUint32(context, args[0]),
                                          .y = convert::ToUint32(context, args[1])};
                args.GetReturnValue().Set(data->Click(cell) == game::ClickResult::Dispatched);
            });

        /// @description Deposits carried gold into this tab. The stash panel must be open. Fire and forget like
        /// `gold()`: `gold` and your gold stats update when the server answers, so poll for the change afterwards.
        /// A shared pool that only supports moving everything ignores `amount`.
        /// @signature depositGold(amount: number)
        /// @param amount {number} - gold to deposit; ignored where the tab only supports moving everything
        /// @returns {boolean} - true once the move was requested; false for a tab that holds no gold, nothing to
        /// move, or not being in a game
        Method(
            cls, "depositGold", +[](const ub::CallbackInfo& args) {
                auto* data = Receiver<JSStashTab>(args);
                if (data == nullptr) {
                    return;
                }
                if (const auto amount = GoldAmount(args)) {
                    args.GetReturnValue().Set(data->MoveGold(game::GoldActionMode::Deposit, *amount));
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
            cls, "withdrawGold", +[](const ub::CallbackInfo& args) {
                auto* data = Receiver<JSStashTab>(args);
                if (data == nullptr) {
                    return;
                }
                if (const auto amount = GoldAmount(args)) {
                    args.GetReturnValue().Set(data->MoveGold(game::GoldActionMode::Withdraw, *amount));
                }
            });
    }

   private:
    // The single amount argument of the gold moves; nullopt (with the error or warning already
    // raised) when the call cannot proceed.
    static std::optional<uint32_t> GoldAmount(const ub::CallbackInfo& args) {
        if (args.Length() < 1 || !args[0].IsNumber()) {
            error::ThrowTypeError(args.GetIsolate(), "StashTab gold moves expect an amount");
            return std::nullopt;
        }
        if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
            error::WarnAndReturnFalse(args, "Game not ready");
            return std::nullopt;
        }
        return convert::ToUint32(args.GetContext(), args[0]);
    }
};

}  // namespace d2bs::api::classes
