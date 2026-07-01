#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ConfigStore.h"
#include "config/ProfileData.h"

namespace d2bs::config {

// ConfigStore implementation backed by a Win32 INI file (d2bs.ini).
// Uses GetPrivateProfileStringW / WritePrivateProfileStringW for persistence.
// INI layout matches the original d2bs reference implementation:
//   [settings]  - global bot settings
//   [<name>]    - one section per profile
class IniConfigStore : public ConfigStore {
   public:
    explicit IniConfigStore(std::filesystem::path iniPath);

    void LoadSettings(AppConfig& config) override;
    std::optional<ProfileData> LoadProfile(const std::string& name) override;
    void SaveProfile(const ProfileData& profile) override;
    bool ProfileExists(const std::string& name) override;
    std::vector<std::string> ListProfiles() override;

   private:
    // Read a string value from a section/key, returning defaultValue on missing key.
    std::string ReadString(const std::string& section, const std::string& key, const std::string& defaultValue) const;

    // Atomically commit a batch of key/value pairs into one section. Serialized
    // across processes by a shared named mutex and written via a temp file +
    // atomic replace, so concurrent d2bsng / D2BotNG writers can neither tear
    // d2bs.ini nor see a half-written profile.
    void WriteKeys(const std::string& section, const std::vector<std::pair<std::string, std::string>>& keyValues) const;

    // Read an integer value from a section/key, returning defaultValue on missing/invalid key.
    int32_t ReadInt(const std::string& section, const std::string& key, int32_t defaultValue) const;

    // Read a floating-point value from a section/key, returning defaultValue on missing/invalid key.
    double ReadDouble(const std::string& section, const std::string& key, double defaultValue) const;

    // Read a boolean value from a section/key. Accepts "true", "t", "1" (case-insensitive) as true.
    bool ReadBool(const std::string& section, const std::string& key, bool defaultValue) const;

    std::filesystem::path path_;
};

}  // namespace d2bs::config
