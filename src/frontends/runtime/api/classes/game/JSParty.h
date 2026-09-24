#pragma once

#include <cstdint>
#include <string_view>
#include <tuple>

#include "Receiver.h"
#include "api/core/Class.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"
#include "game/GameHelpers.h"
#include "game/Party.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

// Party class - represents a player in the party roster
// Used to track other players in the game
class JSParty : public ClassBase<JSParty, game::Party> {
   public:
    static constexpr std::string_view ClassName = "Party";

    static void Configure(const ub::Class<Native>& cls) {
        /// @description The party member's X grid coordinate (world position).
        /// @type {number}
        Property(
            cls, "x", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Pos().x);
            });

        /// @description The party member's Y grid coordinate (world position).
        /// @type {number}
        Property(
            cls, "y", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Pos().y);
            });

        /// @description The level/area ID the party member is currently in.
        /// @type {number}
        Property(
            cls, "area", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->LevelId()));
            });

        /// @description The party member's game/unit ID (GID).
        /// @type {number}
        Property(
            cls, "gid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<double>(data->Id()));
            });

        /// @description The party member's current life value.
        /// @type {number}
        Property(
            cls, "life", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->Life()));
            });

        /// @description The party member's party-relationship flags bitmask (e.g. partied / hostile state).
        /// @type {number}
        Property(
            cls, "partyflag", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->PartyFlag()));
            });

        /// @description The ID of the party the member belongs to (shared by all members of the same party).
        /// @type {number}
        Property(
            cls, "partyid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->PartyId()));
            });

        /// @description The party member's character name.
        /// @type {string}
        Property(
            cls, "name", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                std::ignore = info.GetReturnValue().Set(data->Name());
            });

        /// @description The party member's character class ID (0-6, e.g. Amazon/Sorceress/etc.).
        /// 0 = amazon, 1 = sorceress, 2 = necromancer, 3 = paladin, 4 = barbarian, 5 = druid, 6 = assassin.
        /// @type {number}
        Property(
            cls, "classid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->ClassId()));
            });

        /// @description The party member's character (experience) level.
        /// @type {number}
        Property(
            cls, "level", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
                auto* data = Receiver<JSParty>(info);
                if (data == nullptr || !*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->CharacterLevel()));
            });

        // Methods
        /// @description Advances this Party handle to the next member in the roster chain for chained iteration.
        /// @signature getNext()
        /// @returns {Party|boolean} - This Party object advanced to the next member, or false if the game is not ready
        /// or there is no next member.
        Method(
            cls, "getNext", +[](const ub::CallbackInfo& args) {
                if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                    error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                auto* data = Receiver<JSParty>(args);
                if (data == nullptr) {
                    return;
                }
                if (!*data) {
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
    }
};

}  // namespace d2bs::api::classes
