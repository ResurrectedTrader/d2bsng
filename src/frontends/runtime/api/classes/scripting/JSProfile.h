#pragma once

#include <memory>

#include "api/core/Class.h"
#include "config/ProfileData.h"
#include "unibind/unibind.h"

namespace d2bs::api::classes {

using config::ProfileData;
using config::ProfileType;

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
class JSProfile : public ClassBase<JSProfile, ProfileData> {
   public:
    static constexpr std::string_view ClassName = "Profile";
    // `Profile(...)` without `new` is the reference's spelling; New refuses it unless the
    // profileCallWithoutNew compatibility flag is on.
    static constexpr bool CALLABLE_WITHOUT_NEW = true;

    // Constructor - creates or retrieves a profile
    static std::unique_ptr<ProfileData> New(const ub::CallbackInfo& args);

    static void Configure(const ub::Class<ProfileData>& cls);
};

}  // namespace d2bs::api::classes
