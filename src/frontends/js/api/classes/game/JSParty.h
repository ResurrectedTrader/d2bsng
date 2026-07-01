#pragma once

#include <v8.h>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "config/AppConfig.h"
#include "game/GameHelpers.h"
#include "game/Party.h"

namespace d2bs::api::classes {

// Party class - represents a player in the party roster
// Used to track other players in the game
class JSParty : public V8ClassBase<JSParty, game::Party> {
   public:
    static constexpr std::string_view ClassName = "Party";

    // Party objects are obtained via getParty() global function
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        /// @description The party member's X grid coordinate (world position).
        /// @type {number}
        Property(
            isolate, inst, "x", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Pos().x);
            });

        /// @description The party member's Y grid coordinate (world position).
        /// @type {number}
        Property(
            isolate, inst, "y", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(data->Pos().y);
            });

        /// @description The level/area ID the party member is currently in.
        /// @type {number}
        Property(
            isolate, inst, "area", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->LevelId()));
            });

        /// @description The party member's game/unit ID (GID).
        /// @type {number}
        Property(
            isolate, inst, "gid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), static_cast<double>(data->Id())));
            });

        /// @description The party member's current life value.
        /// @type {number}
        Property(
            isolate, inst, "life", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->Life()));
            });

        /// @description The party member's party-relationship flags bitmask (e.g. partied / hostile state).
        /// @type {number}
        Property(
            isolate, inst, "partyflag", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->PartyFlag()));
            });

        /// @description The ID of the party the member belongs to (shared by all members of the same party).
        /// @type {number}
        Property(
            isolate, inst, "partyid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->PartyId()));
            });

        /// @description The party member's character name.
        /// @type {string}
        Property(
            isolate, inst, "name", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), data->Name()));
            });

        /// @description The party member's character class ID (0-6, e.g. Amazon/Sorceress/etc.).
        /// 0 = amazon, 1 = sorceress, 2 = necromancer, 3 = paladin, 4 = barbarian, 5 = druid, 6 = assassin.
        /// @type {number}
        Property(
            isolate, inst, "classid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
                    return;
                }
                info.GetReturnValue().Set(static_cast<int32_t>(data->ClassId()));
            });

        /// @description The party member's character (experience) level.
        /// @type {number}
        Property(
            isolate, inst, "level", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* data = Unwrap(info.Holder());
                if (!*data) {
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
            isolate, proto, "getNext", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                if (!game::WaitForGameReady(config::GetAppConfig().gameReadyTimeout)) {
                    v8_error::WarnAndReturnFalse(args, "Game not ready");
                    return;
                }
                auto* data = Unwrap(args.This());
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
