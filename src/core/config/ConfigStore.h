#pragma once

#include <optional>
#include <string>
#include <vector>

namespace d2bs::config {

struct AppConfig;
struct ProfileData;

// Abstract interface for loading settings and managing profiles.
// Concrete implementations handle a specific storage backend (INI, JSON, etc.).
class ConfigStore {
   public:
    virtual ~ConfigStore() = default;

    // Load all settings into the existing AppConfig.
    virtual void LoadSettings(AppConfig& config) = 0;

    // Profile CRUD
    virtual std::optional<ProfileData> LoadProfile(const std::string& name) = 0;
    virtual void SaveProfile(const ProfileData& profile) = 0;
    virtual bool ProfileExists(const std::string& name) = 0;
    virtual std::vector<std::string> ListProfiles() = 0;
};

}  // namespace d2bs::config
