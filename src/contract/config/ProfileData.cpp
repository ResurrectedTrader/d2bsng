#include "config/ProfileData.h"

#include "utils/utils.h"

namespace d2bs::config {

ProfileType ModeToProfileType(const std::string& mode) {
    if (mode.empty()) {
        return ProfileType::Invalid;
    }
    switch (utils::ToLower(mode)[0]) {
        case 's':
            return ProfileType::SinglePlayer;
        case 'b':
            return ProfileType::BattleNet;
        case 'o':
            return ProfileType::OpenBattleNet;
        case 'h':
            return ProfileType::TcpIpHost;
        case 'j':
            return ProfileType::TcpIpJoin;
        default:
            return ProfileType::Invalid;
    }
}

std::string ProfileTypeToMode(ProfileType type) {
    switch (type) {
        case ProfileType::SinglePlayer:
            return "single";
        case ProfileType::BattleNet:
            return "battlenet";
        case ProfileType::OpenBattleNet:
            return "open";
        case ProfileType::TcpIpHost:
            return "host";
        case ProfileType::TcpIpJoin:
            return "join";
        default:
            return "invalid";
    }
}

}  // namespace d2bs::config
