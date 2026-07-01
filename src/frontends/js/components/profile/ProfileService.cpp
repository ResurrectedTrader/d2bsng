#include "ProfileService.h"

#include "config/AppConfig.h"
#include "config/ConfigStore.h"

namespace d2bs::profile {

std::optional<config::ProfileData> Load(const std::string& name) {
    auto& cfg = config::GetAppConfig();
    if (!cfg.store) {
        return std::nullopt;
    }
    return cfg.store->LoadProfile(name);
}

std::optional<config::ProfileData> LoadActive() {
    auto name = config::GetAppConfig().GetProfileName();
    if (name.empty()) {
        return std::nullopt;
    }
    return Load(name);
}

std::optional<std::string> ResolveCharacter(const std::string& profileName) {
    auto profile = Load(profileName);
    if (!profile || profile->character.empty()) {
        return std::nullopt;
    }
    return profile->character;
}

bool Switch(const std::string& name) {
    // Validate profile existence without mutating runtime state yet. Using
    // Load() as a fallible "does this profile exist?" check keeps us from
    // writing a stale/nonexistent name into AppConfig.
    if (!Load(name)) {
        return false;
    }

    // SetProfileName publishes the name and clears the waitForProfile latch
    // atomically (release store paired with the game loop's acquire load), so
    // there's no observable window where the latch is clear but the name is
    // stale.
    config::GetAppConfig().SetProfileName(name);
    return true;
}

bool Add(const config::ProfileData& profile) {
    auto& cfg = config::GetAppConfig();
    if (!cfg.store || profile.name.empty()) {
        return false;
    }
    if (cfg.store->ProfileExists(profile.name)) {
        return false;  // idempotent no-op - matches reference addProfile
    }
    cfg.store->SaveProfile(profile);
    return true;
}

}  // namespace d2bs::profile
