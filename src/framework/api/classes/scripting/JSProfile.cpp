#include "JSProfile.h"

#include <algorithm>

#include "components/config/AppConfig.h"
#include "components/config/CompatibilityFlags.h"
#include "components/profile/ProfileService.h"
#include "game/Menu.h"

namespace d2bs::api::classes {

/// @description Construct or retrieve a login Profile.
/// @signature Profile()
/// @returns {Profile} - the active profile.
/// @signature Profile(name: string)
/// @param name {string} - profile name to load from the INI.
/// @returns {Profile} - the named profile.
/// @signature Profile(type: ProfileType, charname: string, diff: Difficulty)
/// @param type {ProfileType} - 1=singlePlayer or 4=tcpIpHost for this form.
/// @param charname {string} - character name.
/// @param diff {Difficulty} - clamped to 0-3 (3 = highest available).
/// @returns {Profile} - a single-player or TCP/IP host profile.
/// @signature Profile(type: ProfileType, charname: string, ip: string)
/// @param type {ProfileType} - 5=tcpIpJoin for this form.
/// @param charname {string} - character name.
/// @param ip {string} - host IP address to join.
/// @returns {Profile} - a TCP/IP join profile.
/// @signature Profile(type: ProfileType, account: string, pass: string, charname: string, gateway: string)
/// @param type {ProfileType} - 2=battleNet or 3=openBattleNet for this form.
/// @param account {string} - account/username.
/// @param pass {string} - account password.
/// @param charname {string} - character name.
/// @param gateway {string} - realm gateway name.
/// @returns {Profile} - a Battle.net profile.
/// @throws {Error} - no-arg form when no active profile name is set ("No active profile!").
/// @throws {Error} - the named (or active) profile is not in the INI ("Profile does not exist").
/// @throws {Error} - args match no valid form, or the ProfileType is wrong for the arg count ("Invalid parameters.").
void JSProfile::New(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    // SpiderMonkey allowed `Profile()` without `new`; kolbot relies on this.
    // When enabled (Compatibility flag: profileCallWithoutNew), auto-redirect to
    // a construct call so the rest of this body runs against a real instance;
    // when disabled, require `new` like an ordinary class.
    if (!args.IsConstructCall()) {
        if (!d2bs::config::CompatibilityFlags::Instance().IsEnabled("profileCallWithoutNew")) {
            v8_error::ThrowTypeError(isolate, "Profile must be called with 'new'");
            return;
        }
        std::vector<v8::Local<v8::Value>> argv(args.Length());
        for (int32_t i = 0; i < args.Length(); ++i) {
            argv[i] = args[i];
        }
        auto func = GetTemplate(isolate)->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Object> instance;
        if (func->NewInstance(context, static_cast<int32_t>(argv.size()), argv.data()).ToLocal(&instance)) {
            args.GetReturnValue().Set(instance);
        }
        return;
    }

    auto argc = args.Length();

    auto data = std::make_unique<ProfileData>();

    // Profile() - get the active profile.
    // Two distinct failure modes vs reference which conflates them:
    //   - No active profile name set -> "No active profile!"
    //   - Active name set but profile missing from INI -> "Profile does not exist"
    //     (matches the 1-arg ctor message).
    // Reference's Profile::init never validates existence; it reads INI with
    // "ERROR" string defaults and returns a stub. That silently hides the
    // bug; explicit throws are clearer.
    if (argc == 0) {
        auto name = d2bs::config::GetAppConfig().GetProfileName();
        if (name.empty()) {
            v8_error::ThrowError(isolate, "No active profile!");
            return;
        }
        auto loaded = d2bs::profile::Load(name);
        if (!loaded) {
            v8_error::ThrowError(isolate, "Profile does not exist");
            return;
        }
        *data = std::move(*loaded);
    }
    // Profile(name) - get the named profile.
    // Same divergence from reference as above: we throw on missing instead
    // of returning an "ERROR"-filled stub.
    else if (argc == 1 && args[0]->IsString()) {
        std::string name = v8_convert::ToString(isolate, args[0]);
        auto loaded = d2bs::profile::Load(name);
        if (!loaded) {
            v8_error::ThrowError(isolate, "Profile does not exist");
            return;
        }
        *data = std::move(*loaded);
    }
    // Profile(ProfileType.singlePlayer, charname, diff)
    else if (argc == 3 && args[0]->IsInt32()) {
        int32_t type = args[0]->Int32Value(context).FromMaybe(0);
        if (type == static_cast<int32_t>(ProfileType::SinglePlayer)) {
            std::string charname = v8_convert::ToString(isolate, args[1]);
            // 0=Normal, 1=Nightmare, 2=Hell, 3=hardest available
            int32_t diff = std::clamp(args[2]->Int32Value(context).FromMaybe(0), 0, 3);
            data->type = ProfileType::SinglePlayer;
            data->character = charname;
            data->difficulty = static_cast<d2bs::game::Difficulty>(diff);
        }
        // Profile(ProfileType.tcpIpHost, charname, diff)
        else if (type == static_cast<int32_t>(ProfileType::TcpIpHost)) {
            std::string charname = v8_convert::ToString(isolate, args[1]);
            // 0=Normal, 1=Nightmare, 2=Hell, 3=hardest available
            int32_t diff = std::clamp(args[2]->Int32Value(context).FromMaybe(0), 0, 3);
            data->type = ProfileType::TcpIpHost;
            data->character = charname;
            data->difficulty = static_cast<d2bs::game::Difficulty>(diff);
        }
        // Profile(ProfileType.tcpIpJoin, charname, ip)
        else if (type == static_cast<int32_t>(ProfileType::TcpIpJoin)) {
            std::string charname = v8_convert::ToString(isolate, args[1]);
            std::string ip = v8_convert::ToString(isolate, args[2]);
            data->type = ProfileType::TcpIpJoin;
            data->character = charname;
            data->ip = ip;
        } else {
            v8_error::ThrowError(isolate, "Invalid parameters.");
            return;
        }
    }
    // Profile(ProfileType.battleNet, account, pass, charname, gateway)
    // Profile(ProfileType.openBattleNet, account, pass, charname, gateway)
    else if (argc == 5 && args[0]->IsInt32()) {
        int32_t type = args[0]->Int32Value(context).FromMaybe(0);
        if (type == static_cast<int32_t>(ProfileType::BattleNet) ||
            type == static_cast<int32_t>(ProfileType::OpenBattleNet)) {
            std::string account = v8_convert::ToString(isolate, args[1]);
            std::string pass = v8_convert::ToString(isolate, args[2]);
            std::string charname = v8_convert::ToString(isolate, args[3]);
            std::string gateway = v8_convert::ToString(isolate, args[4]);
            data->type = static_cast<ProfileType>(type);
            data->username = account;
            data->password = pass;
            data->character = charname;
            data->gateway = gateway;
        } else {
            v8_error::ThrowError(isolate, "Invalid parameters.");
            return;
        }
    } else {
        v8_error::ThrowError(isolate, "Invalid parameters.");
        return;
    }

    InitInstance(isolate, args.This(), std::move(data));
    args.GetReturnValue().Set(args.This());
}

void JSProfile::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    // Properties
    /// @description The profile's connection type.
    /// @type {ProfileType}
    Property(
        isolate, inst, "type", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<int32_t>(data->type)));
        });
    /// @description The host IP address for tcpIpJoin profiles; empty string for others.
    /// @type {string}
    Property(
        isolate, inst, "ip", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->ip));
        });
    /// @description The account/username for battleNet/openBattleNet profiles; empty string for others.
    /// @type {string}
    Property(
        isolate, inst, "username", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->username));
        });
    /// @description The realm gateway name for battleNet/openBattleNet profiles; empty string for others.
    /// @type {string}
    Property(
        isolate, inst, "gateway", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->gateway));
        });
    /// @description The character name associated with the profile; empty string if not set.
    /// @type {string}
    Property(
        isolate, inst, "character", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->character));
        });
    /// @description The profile's configured difficulty.
    /// @type {Difficulty}
    Property(
        isolate, inst, "difficulty",
        +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<int32_t>(data->difficulty)));
        });
    /// @description Maximum time to wait for login, in milliseconds (default 5000).
    /// @type {number}
    Property(
        isolate, inst, "maxLoginTime",
        +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            // JS sees milliseconds as an integer count (reference exposed ms too).
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<int32_t>(data->maxLoginTime.count())));
        });
    /// @description Maximum time to wait at the character-select screen, in milliseconds (default 5000).
    /// @type {number}
    Property(
        isolate, inst, "maxCharacterSelectTime",
        +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();
            auto self = info.Holder();
            auto data = Unwrap(self);
            if (!data) {
                return;
            }
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<int32_t>(data->maxCharTime.count())));
        });

    // Instance Methods
    /// @description Drive the out-of-game login state machine using this profile's settings.
    /// @signature login()
    /// @returns {undefined} - nothing on success; throws an Error carrying the login message on failure.
    /// @throws {Error} - login did not reach Success; the error carries the login status message.
    Method(
        isolate, proto, "login", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto data = Unwrap(args.This());
            if (!data) {
                v8_error::ThrowError(isolate, "Invalid profile object");
                return;
            }
            auto result = d2bs::game::Login(*data);
            if (result.status != d2bs::game::LoginStatus::Success) {
                v8_error::ThrowError(isolate, result.errorMessage);
            }
        });
}

}  // namespace d2bs::api::classes
