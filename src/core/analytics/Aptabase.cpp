#include "analytics/Aptabase.h"

#include <Windows.h>

#include <bcrypt.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <random>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "config/Version.h"
#include "http/Client.h"
#include "utils/crypto.h"
#include "utils/utils.h"

namespace d2bs::core::analytics {

namespace {

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann's conventional alias spelling

// Salt mixed into the derived install id. Fixed, and scoped to the publisher
// rather than to d2bsng: it makes the digest useless for correlating this
// machine against its identifier in anyone else's software, while letting our
// own tools (the bot manager) derive the same id and be joined to it. Bumping it
// re-buckets every install as new.
constexpr std::string_view ID_SALT = "ResurrectedTrader-analytics-v1";

// True when D2BS_VERSION carries a pre-release / build suffix (e.g. "2.0.0-dev").
// Such builds report with isDebug=true so Aptabase buckets them apart from
// released-version traffic rather than dropping them.
bool IsPreReleaseBuild() {
    const std::string_view version = D2BS_VERSION;
    return version.find('-') != std::string_view::npos || version.find('+') != std::string_view::npos;
}

// Read a REG_SZ value, forcing the 64-bit registry view: this DLL is 32-bit, so
// WOW64 would otherwise redirect the HKLM\SOFTWARE read to WOW6432Node, where
// MachineGuid does not live. Empty when absent or unreadable.
std::string ReadRegString(HKEY root, const wchar_t* subKey, const wchar_t* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return {};
    }
    std::array<wchar_t, 256> buffer{};
    // Leave room for a terminator - RegQueryValueExW does not guarantee one.
    DWORD size = sizeof(buffer) - sizeof(wchar_t);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }
    return utils::ToStr(std::wstring(buffer.data()), CP_UTF8);
}

// Serial of the volume Windows is installed on (0 when unavailable). Survives
// reboots and app reinstalls; changes on a reformat.
uint32_t SystemVolumeSerial() {
    std::array<wchar_t, MAX_PATH> winDir{};
    if (GetSystemWindowsDirectoryW(winDir.data(), winDir.size()) == 0) {
        return 0;
    }
    const std::wstring root = std::wstring(winDir.data()).substr(0, 3);  // "C:\"
    if (root.size() < 3) {
        return 0;
    }
    DWORD serial = 0;
    if (GetVolumeInformationW(root.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) == 0) {
        return 0;
    }
    return serial;
}

std::string ComputerName() {
    std::array<wchar_t, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    DWORD size = buffer.size();
    if (GetComputerNameW(buffer.data(), &size) == 0) {
        return {};
    }
    return utils::ToStr(std::wstring(buffer.data(), size), CP_UTF8);
}

// True Windows version (GetVersionExW lies without an app manifest; the ntdll
// RtlGetVersion export reports the real build). "10.0.26100" style.
std::string GetOsVersion() {
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        if (auto* fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            fn(&info);
        }
    }
    return std::format("{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber);
}

// User's locale as a BCP-47 tag, e.g. "en-US"; empty on failure.
std::string GetLocale() {
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> buffer{};
    if (GetUserDefaultLocaleName(buffer.data(), buffer.size()) > 0) {
        return utils::ToStr(buffer.data(), CP_UTF8);
    }
    return {};
}

// ISO-8601 UTC timestamp with millisecond precision, e.g. "2026-07-24T12:34:56.789Z".
std::string IsoNow() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &seconds);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                       utc.tm_hour, utc.tm_min, utc.tm_sec, ms.count());
}

}  // namespace

// A random 128-bit id formatted as a canonical GUID string. Not security
// sensitive (an anonymous install marker), so a non-crypto RNG is fine.
std::string NewRandomId() {
    std::random_device rd;
    const auto draw64 = [&rd] {
        return (static_cast<uint64_t>(rd()) << 32) | rd();
    };
    const uint64_t hi = draw64();
    const uint64_t lo = draw64();
    return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", static_cast<uint32_t>(hi >> 32),
                       static_cast<uint32_t>((hi >> 16) & 0xffff), static_cast<uint32_t>(hi & 0xffff),
                       static_cast<uint32_t>(lo >> 48), lo & 0xffffffffffffULL);
}

// Stable anonymous install id, *derived* rather than stored - analytics writes
// nothing to disk or the registry. Three independent machine facts are combined
// so that a missing or constant one (some Wine prefixes report no MachineGuid)
// still leaves the id distinct, then salted and hashed: only the digest ever
// leaves the machine, and the salt keeps it from matching the same machine's
// identifier in any other software. Falls back to a per-launch random id if
// hashing itself fails, which overcounts rather than collapsing every affected
// install into one bucket.
std::string DeriveInstallId() {
    std::string material(ID_SALT);
    material += '|';
    material += ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid");
    material += '|';
    material += std::to_string(SystemVolumeSerial());
    material += '|';
    material += ComputerName();

    std::string digest = utils::HashString(BCRYPT_SHA256_ALGORITHM, material);
    return !digest.empty() ? digest : NewRandomId();
}

// Identifies the active profile without revealing it: only the digest is sent,
// and mixing the install id into the material scopes it to this install, so the
// same profile name on two machines hashes differently. That keeps profile names
// uncorrelatable across users and un-recoverable by hashing likely names, while
// still letting one install's profiles be counted apart. The name is lowercased
// first: d2bs.ini profile lookup goes through GetPrivateProfileString, which is
// case-insensitive, so "Sorc1" and "sorc1" are one profile and must not hash
// into two.
// Empty when hashing fails, which the caller treats as "profile still unknown".
std::string HashProfileName(std::string_view installId, std::string_view profileName) {
    std::string material(ID_SALT);
    material += "|profile|";
    material += installId;
    material += '|';
    material += utils::ToLower(std::string(profileName));

    const std::string digest = utils::HashString(BCRYPT_SHA256_ALGORITHM, material);
    // Length of the profile digest we report. 64 bits is far more than enough to
    // tell one install's handful of profiles apart, and a short value keeps the
    // dashboard readable.
    constexpr size_t PROFILE_HASH_CHARS = 16;
    return digest.size() > PROFILE_HASH_CHARS ? digest.substr(0, PROFILE_HASH_CHARS) : digest;
}

// Wrap event-specific props in the envelope Aptabase expects and POST it.
bool PostEvent(const EventContext& ctx, const std::shared_ptr<spdlog::logger>& logger, std::string_view eventName,
               json props) {
    json event;
    event["timestamp"] = IsoNow();
    event["sessionId"] = ctx.sessionId;
    event["eventName"] = eventName;

    json systemProps;
    systemProps["isDebug"] = IsPreReleaseBuild();
    systemProps["osName"] = "Windows";
    systemProps["osVersion"] = GetOsVersion();
    systemProps["locale"] = GetLocale();
    systemProps["appVersion"] = D2BS_VERSION;
    systemProps["appBuildNumber"] = "";
    systemProps["sdkVersion"] = std::string(ctx.sdkName) + "@" + D2BS_VERSION;
    event["systemProps"] = std::move(systemProps);

    props["installId"] = ctx.installId;
    event["props"] = std::move(props);

    http::Request request;
    request.method = "POST";
    // Aptabase's single-event ingest path.
    constexpr std::string_view EVENT_PATH = "/api/v0/event";
    request.url = std::string(ctx.host) + std::string(EVENT_PATH);
    request.headers = {
        {"Content-Type", "application/json"},
        {"App-Key", std::string(ctx.appKey)},
        {"User-Agent", std::string(ctx.sdkName) + "-analytics/" + D2BS_VERSION},
    };
    const std::string body = event.dump();
    request.body.assign(body.begin(), body.end());
    constexpr uint32_t NETWORK_TIMEOUT_MS = 10000;
    constexpr uint32_t TOTAL_TIMEOUT_MS = 15000;
    request.timeoutMs = NETWORK_TIMEOUT_MS;
    request.totalTimeoutMs = TOTAL_TIMEOUT_MS;

    http::Response response;
    const std::string error = http::Perform(request, response);
    if (!error.empty()) {
        logger->debug("analytics: {} request failed ({})", eventName, error);
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        logger->debug("analytics: {} HTTP {}", eventName, response.status);
        return false;
    }
    logger->debug("analytics: {} reported", eventName);
    return true;
}

}  // namespace d2bs::core::analytics
