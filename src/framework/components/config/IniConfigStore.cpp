#include "IniConfigStore.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

#include "AppConfig.h"
#include "components/profile/ProfileData.h"
#include "components/speedhack/Speedhack.h"
#include "utils/NamedMutex.h"
#include "utils/utils.h"

namespace d2bs::config {

namespace {
// Shared cross-process lock guarding every d2bs.ini read-modify-write. D2BotNG
// takes a System.Threading.Mutex of the SAME name, so
// its full-file profile rewrites and our addProfile writes serialize against
// each other instead of clobbering one another.
constexpr const wchar_t* INI_LOCK_NAME = L"Local\\d2bs-ini-lock";

// The final atomic swap can transiently fail when a reader briefly holds the
// file open (GetPrivateProfileString); retry a few times before giving up.
constexpr int32_t REPLACE_ATTEMPTS = 5;
constexpr uint32_t REPLACE_RETRY_MS = 20;
}  // namespace

IniConfigStore::IniConfigStore(std::filesystem::path iniPath) : path_(std::move(iniPath)) {}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void IniConfigStore::LoadSettings(AppConfig& config) {
    ScriptPaths paths;
    paths.basePath = path_.parent_path() / ReadString("settings", "ScriptPath", "scripts");
    paths.gameScript = ReadString("settings", "DefaultGameScript", "default.dbj");
    paths.starterScript = ReadString("settings", "DefaultStarterScript", "starter.dbj");
    paths.consoleScript = ReadString("settings", "DefaultConsoleScript", "");
    config.SetDefaultScriptPaths(paths);
    config.SetScriptPaths(std::move(paths));
    config.maxGameTime.store(std::chrono::milliseconds{std::abs(ReadInt("settings", "MaxGameTime", 0)) * 1000});
    config.quitOnHostile.store(ReadBool("settings", "QuitOnHostile", false));
    config.quitOnError.store(ReadBool("settings", "QuitOnError", false));
    config.startAtMenu.store(ReadBool("settings", "StartAtMenu", true));
    config.waitForProfile.store(ReadBool("settings", "UseProfileScript", false));
    config.enableUnsupported.store(ReadBool("settings", "EnableUnsupported", false));
    // InspectorPort: the sign encodes enabled/disabled (see AppConfig), the
    // magnitude is the listening port. 0 (default) disables. Clamp the magnitude
    // into the valid range for either sign (without negating the raw value, so a
    // malformed INT32_MIN can't trip UB): positive stays in [MIN, MAX], negative
    // (disabled, remembered) stays in [-MAX, -MIN].
    const int32_t rawInspectorPort = ReadInt("settings", "InspectorPort", 0);
    int32_t inspectorPort = 0;
    if (rawInspectorPort > 0) {
        inspectorPort = std::clamp(rawInspectorPort, MIN_INSPECTOR_PORT, MAX_INSPECTOR_PORT);
    } else if (rawInspectorPort < 0) {
        inspectorPort = std::clamp(rawInspectorPort, -MAX_INSPECTOR_PORT, -MIN_INSPECTOR_PORT);
    }
    config.inspectorPort.store(inspectorPort);
    config.gameReadyTimeout = std::chrono::milliseconds{ReadInt("settings", "GameReadyTimeout", 5) * 1000};
    config.memoryLimit = static_cast<size_t>(ReadInt("settings", "MemoryLimit", 100)) * 1024 * 1024;
    // Extra V8 flags for SetFlagsFromString (e.g. "--max-old-space-size=512").
    config.v8Flags = ReadString("settings", "V8Flags", "");
    // V8 default-platform worker pool size; 0 = auto, clamped to [0, 64].
    config.v8ThreadPoolSize = std::clamp(ReadInt("settings", "V8ThreadPoolSize", 0), 0, 64);
    config.v8SingleThreadedPlatform = ReadBool("settings", "V8SingleThreadedPlatform", false);
    // Idle-sleep granularity (ms), clamped to [1, 100] (0 would busy-spin).
    config.idleSleepInterval =
        std::chrono::milliseconds{std::clamp(ReadInt("settings", "IdleSleepIntervalMs", 10), 1, 100)};
    // Unlike the other atomic fields above, `speed` is not written via
    // .store() directly: speedhack::SetSpeed re-anchors the time-domain bases
    // before publishing the new value to AppConfig.speed, which is required
    // for virtual time to stay continuous across a speed change. A value of
    // 1.0 short-circuits to a no-op inside SetSpeed.
    speedhack::SetSpeed(static_cast<float>(ReadDouble("settings", "Speed", 1.0)));
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

std::optional<ProfileData> IniConfigStore::LoadProfile(const std::string& name) {
    if (name.empty() || !ProfileExists(name)) {
        return std::nullopt;
    }

    ProfileData profile;
    profile.name = name;

    std::string mode = ReadString(name, "mode", "single");
    profile.type = ModeToProfileType(mode);

    profile.character = ReadString(name, "character", "");
    profile.username = ReadString(name, "username", "");
    profile.password = ReadString(name, "password", "");
    profile.gateway = ReadString(name, "gateway", "");
    // The reference d2bs used a union { ip[16]; username[48]; } - for TCP/IP Join
    // profiles, the IP address was stored in the "username" INI key (shared memory).
    // We separate ip/username into distinct fields with a dedicated "ip" INI key.
    // For backward compatibility with old d2bs.ini files that lack an "ip" key,
    // fall back to reading the IP from "username" for TCP/IP Join profiles.
    profile.ip = ReadString(name, "ip", "");
    if (profile.ip.empty() && profile.type == ProfileType::TcpIpJoin) {
        profile.ip = profile.username;
    }
    // Clamp raw INI value to [0..3]; any out-of-range integer falls back to Normal
    // so we don't expose the undefined enum value to the state machine.
    auto rawDiff = ReadInt(name, "spdifficulty", 0);
    profile.difficulty =
        (rawDiff >= 0 && rawDiff <= 3) ? static_cast<d2bs::game::Difficulty>(rawDiff) : d2bs::game::Difficulty::Normal;

    // maxLoginTime / maxCharTime are read from [settings], matching reference behavior.
    // INI stores seconds; convert to chrono::milliseconds at the boundary.
    profile.maxLoginTime = std::chrono::milliseconds{ReadInt("settings", "MaxLoginTime", 5) * 1000};
    profile.maxCharTime = std::chrono::milliseconds{ReadInt("settings", "MaxCharSelectTime", 5) * 1000};

    // Per-profile script overrides (reference Helpers.cpp:88-91). Empty default
    // means "inherit from AppConfig" - only non-empty values override on Switch.
    profile.scriptPaths.basePath = ReadString(name, "ScriptPath", "");
    profile.scriptPaths.starterScript = ReadString(name, "DefaultStarterScript", "");
    profile.scriptPaths.gameScript = ReadString(name, "DefaultGameScript", "");
    profile.scriptPaths.consoleScript = ReadString(name, "DefaultConsoleScript", "");

    return profile;
}

void IniConfigStore::SaveProfile(const ProfileData& profile) {
    // Batch every key into one locked, atomic transaction so the section lands
    // all-or-nothing (see WriteKeys). For TcpIpJoin the
    // reference stores the IP in the "username" field (union); we also write the
    // dedicated "ip" key, so old d2bs and d2bsng both round-trip.
    const std::vector<std::pair<std::string, std::string>> keyValues = {
        {"mode", ProfileTypeToMode(profile.type)},
        {"character", profile.character},
        {"username", profile.type == ProfileType::TcpIpJoin ? profile.ip : profile.username},
        {"password", profile.password},
        {"gateway", profile.gateway},
        {"ip", profile.ip},
        {"spdifficulty", std::to_string(static_cast<int32_t>(profile.difficulty))},
    };
    WriteKeys(profile.name, keyValues);
    // Per-profile script overrides (ScriptPath / DefaultStarterScript /
    // DefaultGameScript / DefaultConsoleScript) are read-only INI fields,
    // hand-edited by users (matches reference JSMenu.cpp:140-150 addProfile,
    // which writes only login-related keys).
}

bool IniConfigStore::ProfileExists(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    std::wstring widePath = path_.wstring();
    std::wstring wideName = utils::ToWStr(name);

    // GetPrivateProfileStringW with NULL section/key enumerates section names.
    // Each name is null-terminated, list ends with double null.
    std::array<wchar_t, 65535> sections{};
    auto count = GetPrivateProfileStringW(nullptr, nullptr, nullptr, sections.data(),
                                          static_cast<DWORD>(sections.size()), widePath.c_str());
    if (count == 0) {
        return false;
    }

    size_t offset = 0;
    while (offset < count) {
        const wchar_t* current = sections.data() + offset;
        if (utils::EqualsCaseInsensitive(std::wstring_view(current), wideName)) {
            return true;
        }
        offset += wcslen(current) + 1;
    }
    return false;
}

std::vector<std::string> IniConfigStore::ListProfiles() {
    std::wstring widePath = path_.wstring();

    std::array<wchar_t, 65535> sections{};
    auto count = GetPrivateProfileStringW(nullptr, nullptr, nullptr, sections.data(),
                                          static_cast<DWORD>(sections.size()), widePath.c_str());

    std::vector<std::string> result;
    size_t offset = 0;
    while (offset < count) {
        const wchar_t* current = sections.data() + offset;
        std::wstring sectionName(current);

        // Skip the [settings] section - it's not a profile.
        if (!utils::EqualsCaseInsensitive(sectionName, std::wstring_view(L"settings"))) {
            result.push_back(utils::ToStr(sectionName));
        }
        offset += sectionName.size() + 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string IniConfigStore::ReadString(const std::string& section, const std::string& key,
                                       const std::string& defaultValue) const {
    std::wstring wideSection = utils::ToWStr(section);
    std::wstring wideKey = utils::ToWStr(key);
    std::wstring wideDefault = utils::ToWStr(defaultValue);
    std::wstring widePath = path_.wstring();

    std::array<wchar_t, 1024> buffer{};
    GetPrivateProfileStringW(wideSection.c_str(), wideKey.c_str(), wideDefault.c_str(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), widePath.c_str());
    return utils::ToStr(buffer.data());
}

void IniConfigStore::WriteKeys(const std::string& section,
                               const std::vector<std::pair<std::string, std::string>>& keyValues) const {
    // Serialize the whole read-modify-write across processes so concurrent
    // writers (other d2bsng instances, D2BotNG) can't lose each other's updates.
    const d2bs::utils::NamedMutexLock lock(INI_LOCK_NAME);
    // Acquired() may be false on timeout; we still proceed - the commit below is
    // an atomic replace, so the worst case is a lost update, never a torn file.

    const std::wstring widePath = path_.wstring();
    const std::wstring wideDir = path_.parent_path().wstring();

    // Stage the edit on a private temp file in the same directory (same volume,
    // so the final move is atomic), then swap it into place. WritePrivateProfile
    // leaves the [settings] section and any foreign keys/comments untouched.
    std::array<wchar_t, MAX_PATH> tempPath{};
    if (GetTempFileNameW(wideDir.c_str(), L"d2b", 0, tempPath.data()) == 0) {
        return;  // can't stage safely - leave the original untouched
    }

    const bool originalExists = std::filesystem::exists(path_);
    if (originalExists && CopyFileW(widePath.c_str(), tempPath.data(), FALSE) == 0) {
        DeleteFileW(tempPath.data());
        return;
    }

    const std::wstring wideSection = utils::ToWStr(section);
    for (const auto& [key, value] : keyValues) {
        WritePrivateProfileStringW(wideSection.c_str(), utils::ToWStr(key).c_str(), utils::ToWStr(value).c_str(),
                                   tempPath.data());
    }
    // Flush WritePrivateProfile's per-process cache out to the temp before swap.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, tempPath.data());

    for (int32_t attempt = 0; attempt < REPLACE_ATTEMPTS; ++attempt) {
        const BOOL ok = originalExists ? ReplaceFileW(widePath.c_str(), tempPath.data(), nullptr, 0, nullptr, nullptr)
                                       : MoveFileExW(tempPath.data(), widePath.c_str(),
                                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (ok != 0) {
            return;
        }
        Sleep(REPLACE_RETRY_MS);
    }
    DeleteFileW(tempPath.data());  // every attempt failed - leave the original intact
}

int32_t IniConfigStore::ReadInt(const std::string& section, const std::string& key, int32_t defaultValue) const {
    std::string str = ReadString(section, key, std::to_string(defaultValue));
    try {
        return static_cast<int32_t>(std::stol(str));
    } catch (...) {
        return defaultValue;
    }
}

double IniConfigStore::ReadDouble(const std::string& section, const std::string& key, double defaultValue) const {
    std::string str = ReadString(section, key, std::to_string(defaultValue));
    try {
        return std::stod(str);
    } catch (...) {
        return defaultValue;
    }
}

bool IniConfigStore::ReadBool(const std::string& section, const std::string& key, bool defaultValue) const {
    auto val = ReadString(section, key, defaultValue ? "true" : "false");
    if (val.empty())
        return defaultValue;
    char first = static_cast<char>(std::tolower(static_cast<unsigned char>(val[0])));
    return first == 't' || first == '1';
}

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
