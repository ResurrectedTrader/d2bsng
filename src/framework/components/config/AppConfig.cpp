// ConfigStore.h must precede AppConfig.h so that ConfigStore is a complete type
// when unique_ptr<ConfigStore> member initializers are parsed.
#include "ConfigStore.h"

#include "AppConfig.h"
#include "utils/utils.h"

namespace d2bs::config {

AppConfig::AppConfig() = default;
AppConfig::~AppConfig() = default;

AppConfig& GetAppConfig() {
    static AppConfig config;
    return config;
}

std::string AppConfig::GetProfileName() const {
    std::shared_lock lock(stateMutex_);
    return profileName_;
}

void AppConfig::SetProfileName(std::string name) {
    std::unique_lock lock(stateMutex_);
    profileName_ = std::move(name);
    // Publishing a real profile name clears the wait-for-profile latch - the
    // game loop reads waitForProfile with memory_order_acquire, and the release
    // store here pairs with that acquire so a snapshot observing the cleared
    // latch is guaranteed to see the new name. Empty names are a no-op so a
    // reset-to-empty doesn't auto-launch the default starter against no profile.
    if (!profileName_.empty()) {
        waitForProfile.store(false, std::memory_order_release);
    }
}

ScriptPaths AppConfig::GetScriptPaths() const {
    std::shared_lock lock(stateMutex_);
    return scriptPaths_;
}

void AppConfig::SetScriptPaths(ScriptPaths paths) {
    std::unique_lock lock(stateMutex_);
    scriptPaths_ = std::move(paths);
}

ScriptPaths AppConfig::GetDefaultScriptPaths() const {
    std::shared_lock lock(stateMutex_);
    return defaultScriptPaths_;
}

void AppConfig::SetDefaultScriptPaths(ScriptPaths paths) {
    std::unique_lock lock(stateMutex_);
    defaultScriptPaths_ = std::move(paths);
}

namespace {
// Characters not allowed in relative paths (matches reference: "\":?*<>|)
bool ContainsInvalidChars(const std::string& path) {
    return path.find_first_of("\":?*<>|") != std::string::npos;
}
}  // namespace

bool IsValidPath(const std::string& relativePath) {
    return !GetPathRelScript(relativePath).empty();
}

std::filesystem::path GetPathRelScript(const std::string& relativePath) {
    if (relativePath.empty()) {
        return {};
    }

    // Block directory traversal sequences and invalid characters
    if (relativePath.find("..\\") != std::string::npos || relativePath.find("../") != std::string::npos ||
        ContainsInvalidChars(relativePath)) {
        return {};
    }

    const auto basePath = GetAppConfig().GetScriptPaths().basePath;
    if (basePath.empty()) {
        return {};
    }

    // Construct full path and normalize
    std::filesystem::path fullPath;
    std::filesystem::path canonBase;
    try {
        fullPath = std::filesystem::weakly_canonical(basePath / relativePath);
        canonBase = std::filesystem::weakly_canonical(basePath);
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }

    // Verify the resolved path is still under the script base directory.
    const auto& fullStr = fullPath.native();
    const auto& baseStr = canonBase.native();

    // Case-insensitive prefix check (Windows/NTFS is case-insensitive).
    auto fullLower = d2bs::utils::ToLower(std::wstring(fullStr));
    auto baseLower = d2bs::utils::ToLower(std::wstring(baseStr));
    if (!fullLower.starts_with(baseLower)) {
        return {};
    }

    // Ensure the character after the base path is a separator (or the paths are equal)
    if (fullStr.size() > baseStr.size() && fullStr[baseStr.size()] != L'\\' && fullStr[baseStr.size()] != L'/') {
        return {};
    }

    return fullPath;
}

}  // namespace d2bs::config
