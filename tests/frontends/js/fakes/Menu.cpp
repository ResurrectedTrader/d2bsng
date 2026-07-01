#include "game/Menu.h"

namespace d2bs::game {

LoginResult Login(const config::ProfileData& /*profile*/) {
    return {.status = LoginStatus::Error, .errorMessage = "Login not implemented (test fake)"};
}
bool SelectCharacter(const std::string& /*charName*/) {
    return false;
}
bool CreateCharacter(const std::string& /*name*/, CharacterClass /*charClass*/, bool /*isHardcore*/,
                     bool /*isLadder*/) {
    return false;
}
bool CreateGame(const std::string& /*name*/, const std::string& /*password*/, Difficulty /*difficulty*/) {
    return false;
}
bool JoinGame(const std::string& /*name*/, const std::string& /*password*/) {
    return false;
}
OutOfGameLocation GetOutOfGameLocation() {
    return OutOfGameLocation::PreSplash;
}

}  // namespace d2bs::game
