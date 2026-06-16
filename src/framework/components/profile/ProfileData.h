#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "components/config/ScriptPaths.h"
#include "game/Types.h"

namespace d2bs::config {

// ============================================================================
// Profile Type Constants
// ============================================================================
enum class ProfileType : int32_t {
    Invalid = 0,
    SinglePlayer = 1,
    BattleNet = 2,
    OpenBattleNet = 3,
    TcpIpHost = 4,
    TcpIpJoin = 5
};

// Profile data - persisted via ConfigStore, used by the JS Profile class.
// Relocated from api/classes/scripting/JSProfile.h so config/ owns the struct
// and api/ can depend on it without a dependency inversion.
struct ProfileData {
    std::string name;
    ProfileType type = ProfileType::Invalid;
    std::string ip;
    std::string username;
    std::string password;
    std::string gateway;
    std::string character;
    // Difficulty is stored as an INI integer (0-3, per reference 'spdifficulty')
    // but carried through the codebase as an enum so intent is explicit.
    d2bs::game::Difficulty difficulty = d2bs::game::Difficulty::Normal;
    // Reference stores MaxLoginTime / MaxCharSelectTime in seconds in the INI
    // and *1000s to ms at load; we keep the ms values as chrono::milliseconds
    // so arithmetic at the consumer (Login state machine) is type-safe.
    std::chrono::milliseconds maxLoginTime{5000};
    std::chrono::milliseconds maxCharTime{5000};

    // Per-profile script overrides. Empty fields inherit from AppConfig; non-empty
    // values are merged into AppConfig.scriptPaths by GameLoop on profile switch.
    ScriptPaths scriptPaths;
};

// Convert a mode string (first character) to a ProfileType.
// Accepted first letters: s (SinglePlayer), b (BattleNet), o (OpenBattleNet),
// h (TcpIpHost), j (TcpIpJoin). Anything else returns Invalid.
ProfileType ModeToProfileType(const std::string& mode);

// Convert a ProfileType to the mode string stored in the INI.
std::string ProfileTypeToMode(ProfileType type);

}  // namespace d2bs::config
