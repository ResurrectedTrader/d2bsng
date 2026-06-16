#pragma once

#include <v8.h>
#include <cstdint>
#include <string>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/globals/Constants.h"
#include "components/profile/ProfileData.h"

namespace d2bs::api::classes {

using d2bs::config::ProfileData;
using d2bs::config::ProfileType;

// Profile class - represents a login profile configuration
// Properties: type, ip, username, gateway, character, difficulty, maxLoginTime, maxCharTime
// Instance methods: login
//
// Constructor signatures:
//   Profile() - get the active profile
//   Profile(name) - get the named profile
//   Profile(ProfileType.singlePlayer, charname, diff)
//   Profile(ProfileType.battleNet, account, pass, charname, gateway)
//   Profile(ProfileType.openBattleNet, account, pass, charname, gateway)
//   Profile(ProfileType.tcpIpHost, charname, diff)
//   Profile(ProfileType.tcpIpJoin, charname, ip)
class JSProfile : public V8ClassBase<JSProfile, ProfileData> {
   public:
    static constexpr std::string_view ClassName = "Profile";

    // Constructor - creates or retrieves a profile
    static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::api::classes
