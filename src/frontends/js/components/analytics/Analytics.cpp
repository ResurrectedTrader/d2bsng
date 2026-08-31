#include "components/analytics/Analytics.h"

#include <Windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <exception>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

// The V8-free HTTP engine (WinHTTP; bypasses the game's SOCKS5 detour). Reused
// here rather than duplicating the WinHTTP plumbing - a documented
// components -> api exception (HttpEngine.h pulls in no V8 / JS headers), the
// same one the update checker relies on.
#include "api/classes/io/HttpEngine.h"
#include "config/AppConfig.h"
#include "config/CompatibilityFlags.h"
#include "config/Version.h"
#include "game/GameHelpers.h"
#include "utils/crypto.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::js::analytics {

namespace {

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann's conventional alias spelling

// Short settle delay before the first send so it doesn't pile onto the heavy
// DLL-load / framework-init work.
constexpr auto INITIAL_DELAY = std::chrono::seconds{5};
// Retry a failed POST a few times (the network may not be up at inject time),
// backing off between attempts.
constexpr auto RETRY_INTERVAL = std::chrono::seconds{30};
constexpr int32_t MAX_ATTEMPTS = 3;

constexpr uint32_t NETWORK_TIMEOUT_MS = 10000;
constexpr uint32_t TOTAL_TIMEOUT_MS = 15000;

// How often the reporter re-reads the active profile name. A profile lives for a
// whole bot run, so this only bounds how quickly a switch is noticed; the poll
// itself is a string copy against a shared_mutex.
constexpr auto PROFILE_POLL_INTERVAL = std::chrono::seconds{5};

// Aptabase's single-event ingest path.
constexpr std::string_view EVENT_PATH = "/api/v0/event";

// Salt mixed into the derived install id. Fixed, and scoped to the publisher
// rather than to d2bsng: it makes the digest useless for correlating this
// machine against its identifier in anyone else's software, while letting our
// own tools (the bot manager) derive the same id and be joined to it. Bumping it
// re-buckets every install as new.
constexpr std::string_view ID_SALT = "ResurrectedTrader-analytics-v1";

// Length of the profile digest we report. 64 bits is far more than enough to
// tell one install's handful of profiles apart, and a short value keeps the
// dashboard readable.
constexpr size_t PROFILE_HASH_CHARS = 16;

#ifdef _WIN64
constexpr std::string_view ARCH = "x64";
#else
constexpr std::string_view ARCH = "x86";
#endif

// Speeds within this of 1.0 count as "no speedhack" (the default is exactly 1.0).
constexpr float SPEED_EPSILON = 0.0001F;

// Aptabase app key, baked in at build time exactly like D2BS_VERSION: the
// #ifndef fallback defaults it to an empty string, and CI overrides it via
// /D D2BS_ANALYTICS_KEY="A-XX-..." (build.ps1 -AnalyticsKey / MSBuild
// -p:D2bsAnalyticsKey=...). An empty key disables analytics, so a build without
// the define is a no-op. There is no runtime app-key override.
#ifndef D2BS_ANALYTICS_KEY
    #define D2BS_ANALYTICS_KEY ""
#endif
// The empty default is the deliberate "analytics off" sentinel, not a redundant init.
// NOLINTNEXTLINE(readability-redundant-string-init)
constexpr std::string_view EMBEDDED_APP_KEY = D2BS_ANALYTICS_KEY;

// Read an environment variable as UTF-8; empty string when unset or empty.
std::string GetEnv(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), needed);
    buffer.resize(written);
    return utils::ToStr(buffer, CP_UTF8);
}

// A launcher-supplied flag counts as "on" for any value except a clear falsey
// one, so both `D2BS_ANALYTICS_DISABLE=1` and `=true` opt out.
bool IsTruthy(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(value.front())));
    return first != '0' && first != 'f' && first != 'n';
}

// True when D2BS_VERSION carries a pre-release / build suffix (e.g. "2.0.0-dev").
// Such builds report with isDebug=true so Aptabase buckets them apart from
// released-version traffic rather than dropping them.
bool IsPreReleaseBuild() {
    const std::string_view version = D2BS_VERSION;
    return version.find('-') != std::string_view::npos || version.find('+') != std::string_view::npos;
}

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
    if (GetSystemWindowsDirectoryW(winDir.data(), static_cast<UINT>(winDir.size())) == 0) {
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
    return digest.size() > PROFILE_HASH_CHARS ? digest.substr(0, PROFILE_HASH_CHARS) : digest;
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

// Wine version string when running under Wine (very common for D2 bots on
// Linux), empty on native Windows. Wine exports wine_get_version from ntdll.
std::string WineVersion() {
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        using WineGetVersionFn = const char*(__cdecl*)();
        if (auto* fn = reinterpret_cast<WineGetVersionFn>(GetProcAddress(ntdll, "wine_get_version"))) {
            const char* version = fn();
            return version != nullptr ? std::string(version) : std::string("unknown");
        }
    }
    return {};
}

// Logical processor count (0 if the runtime can't tell).
uint32_t CpuCores() {
    return std::thread::hardware_concurrency();
}

// Total physical RAM in MiB (0 on failure).
uint64_t TotalRamMb() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) != 0) {
        return status.ullTotalPhys / (1024ULL * 1024ULL);
    }
    return 0;
}

// User's locale as a BCP-47 tag, e.g. "en-US"; empty on failure.
std::string GetLocale() {
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> buffer{};
    if (GetUserDefaultLocaleName(buffer.data(), static_cast<int>(buffer.size())) > 0) {
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

// Compatibility flags that are NOT at their registered default, as `-name`
// (turned off) / `+name` (turned on), sorted. Only deviations are worth sending:
// every flag sitting at its default is the norm and would drown the signal, while
// the deviations are exactly what says whether a legacy shim is still load-bearing
// or safe to retire. Scripts are the only thing that toggles these (the
// `Compatibility` JS object), so this is the state as of the settle delay.
std::vector<std::string> CompatibilityOverrides() {
    std::vector<std::string> overrides;
    for (const auto& flag : config::CompatibilityFlags::Instance().All()) {
        if (flag.enabled != flag.defaultEnabled) {
            overrides.push_back((flag.enabled ? "+" : "-") + flag.name);
        }
    }
    std::ranges::sort(overrides);
    return overrides;
}

// Join strings with a separator, e.g. {"a","b"} -> "a,b".
std::string Join(const std::vector<std::string>& items, char sep) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += sep;
        }
        out += item;
    }
    return out;
}

// Map an Aptabase app key ("A-<REGION>-<digits>") to its ingest base URL. A
// D2BS_ANALYTICS_HOST override wins (needed for self-hosted "A-SH-" / dev
// "A-DEV-" keys); otherwise US / EU are derived. Empty => can't route.
std::string ResolveHost(std::string_view key) {
    if (std::string hostOverride = GetEnv(L"D2BS_ANALYTICS_HOST"); !hostOverride.empty()) {
        // Drop a trailing slash so EVENT_PATH joins cleanly.
        while (!hostOverride.empty() && hostOverride.back() == '/') {
            hostOverride.pop_back();
        }
        return hostOverride;
    }

    // Region is the token between the first two dashes: A-EU-123 -> "EU".
    const size_t firstDash = key.find('-');
    if (firstDash == std::string_view::npos) {
        return {};
    }
    const size_t secondDash = key.find('-', firstDash + 1);
    if (secondDash == std::string_view::npos) {
        return {};
    }
    const std::string_view region = key.substr(firstDash + 1, secondDash - firstDash - 1);
    if (region == "US") {
        return "https://us.aptabase.com";
    }
    if (region == "EU") {
        return "https://eu.aptabase.com";
    }
    return {};
}

// Where the launch reports to and who it is, as carried by every event.
struct EventContext {
    std::string_view host;
    std::string_view sessionId;
    std::string_view installId;
};

// Wrap event-specific props in the envelope Aptabase expects and POST it.
// Returns true on a 2xx response.
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
    systemProps["sdkVersion"] = std::string("d2bsng@") + D2BS_VERSION;
    event["systemProps"] = std::move(systemProps);

    props["installId"] = ctx.installId;
    event["props"] = std::move(props);

    api::classes::HttpRequest request;
    request.method = "POST";
    request.url = std::string(ctx.host) + std::string(EVENT_PATH);
    request.headers = {
        {"Content-Type", "application/json"},
        {"App-Key", std::string(EMBEDDED_APP_KEY)},
        {"User-Agent", std::string("d2bsng-analytics/") + D2BS_VERSION},
    };
    const std::string body = event.dump();
    request.body.assign(body.begin(), body.end());
    request.timeoutMs = NETWORK_TIMEOUT_MS;
    request.totalTimeoutMs = TOTAL_TIMEOUT_MS;

    api::classes::HttpResponse response;
    const std::string error = api::classes::PerformHttpRequest(request, response);
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

}  // namespace

Analytics& Analytics::Instance() {
    static Analytics instance;
    return instance;
}

Analytics::Analytics() {
    logger_ = utils::GetLogger("analytics");
}

Analytics::~Analytics() {
    Stop();
}

void Analytics::Start() {
    const auto launch = game::GetAnalyticsLaunchOptions();

    if (launch.disabled || IsTruthy(GetEnv(L"D2BS_ANALYTICS_DISABLE"))) {
        logger_->debug("analytics disabled by opt-out");
        return;
    }

    // The app key is baked in at build time only (D2BS_ANALYTICS_KEY); an empty
    // key means the build shipped without it, so analytics is a no-op.
    if (EMBEDDED_APP_KEY.empty()) {
        logger_->debug("analytics disabled - no app key compiled in");
        return;
    }

    std::string host = ResolveHost(EMBEDDED_APP_KEY);
    if (host.empty()) {
        logger_->warn("analytics disabled - can't derive ingest host from app key (set D2BS_ANALYTICS_HOST)");
        return;
    }

    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;  // already running
    }

    // Published only once the CAS is won, so a losing caller can't race the
    // running reporter thread's read of it.
    host_ = std::move(host);
    thread_ = std::jthread([this](const std::stop_token& stopToken) { Run(stopToken); });
}

void Analytics::Stop() {
    if (!started_.exchange(false)) {
        return;  // not running
    }
    if (thread_.joinable()) {
        thread_.request_stop();
        // request_stop() already wakes the interruptible wait_for via its
        // registered stop_token; this notify is a harmless backstop.
        cv_.notify_all();
        thread_.join();
    }
}

void Analytics::Run(const std::stop_token& stopToken) {
    thread_utils::SetThreadDescription("d2bs analytics");

    // Top frame of the reporter thread, so an escaping exception is
    // std::terminate() and takes the game with it. std::random_device (session
    // id) throws when the OS exposes no entropy source, and json::dump() throws
    // on invalid UTF-8; losing the telemetry is the correct outcome for both.
    try {
        std::unique_lock lock(mutex_);
        if (WaitFor(lock, stopToken, INITIAL_DELAY)) {
            return;
        }

        installId_ = DeriveInstallId();
        sessionId_ = NewRandomId();

        for (int32_t attempt = 0; attempt < MAX_ATTEMPTS && !stopToken.stop_requested(); ++attempt) {
            lock.unlock();
            const bool sent = SendStartupEvent();
            lock.lock();
            if (sent) {
                break;
            }
            // Back off before retrying; wakes early when Stop() requests it. Skipped
            // after the final attempt so giving up doesn't stall the profile watch.
            if (attempt + 1 < MAX_ATTEMPTS && WaitFor(lock, stopToken, RETRY_INTERVAL)) {
                return;
            }
        }

        // Profiles are watched even if session_start never landed - the network may
        // come back later in the launch, and a profile event is worth having on its own.
        WatchProfiles(lock, stopToken);
    } catch (const std::exception& e) {
        logger_->debug("analytics reporter stopped: {}", e.what());
    } catch (...) {
        logger_->debug("analytics reporter stopped: unknown exception");
    }
}

bool Analytics::WaitFor(std::unique_lock<std::mutex>& lock, const std::stop_token& stopToken,
                        std::chrono::milliseconds duration) {
    // wait_for returns the predicate result, so true means Stop() fired.
    return cv_.wait_for(lock, stopToken, duration, [&stopToken] { return stopToken.stop_requested(); });
}

void Analytics::WatchProfiles(std::unique_lock<std::mutex>& lock, const std::stop_token& stopToken) {
    std::unordered_set<std::string> reported;
    // A hash held back because its POST failed; retried before any newer profile
    // is looked at, so the events keep the order the profiles were used in.
    std::string pending;
    int32_t attempts = 0;

    while (!stopToken.stop_requested()) {
        if (WaitFor(lock, stopToken, PROFILE_POLL_INTERVAL)) {
            return;
        }

        if (pending.empty()) {
            const std::string name = config::GetAppConfig().GetProfileName();
            if (name.empty()) {
                continue;  // no profile yet - a script hasn't logged in
            }
            std::string hash = HashProfileName(installId_, name);
            if (hash.empty() || reported.contains(hash)) {
                continue;
            }
            pending = std::move(hash);
            attempts = 0;
        }

        lock.unlock();
        const bool sent = SendProfileEvent(pending);
        lock.lock();
        // Give up after the same few attempts session_start gets, so a dead
        // network can't turn the poll into an endless retry. Either way the
        // profile counts as handled and a switch back to it won't re-report.
        if (sent || ++attempts >= MAX_ATTEMPTS) {
            reported.insert(std::move(pending));
            pending.clear();
        }
    }
}

bool Analytics::SendStartupEvent() {
    json props;
    props["backendVersion"] = game::GetBackendVersion();
    props["arch"] = std::string(ARCH);

    // Version of the bot manager that spawned the game, when one did and it
    // advertises itself. Omitted rather than sent empty so "no manager" and
    // "manager too old to advertise" stay one bucket instead of two.
    if (const std::string managerVersion = GetEnv(L"D2BOTNG_VERSION"); !managerVersion.empty()) {
        props["managerVersion"] = managerVersion;
    }

    // Runtime environment / hardware.
    const std::string wine = WineVersion();
    props["isWine"] = !wine.empty();
    if (!wine.empty()) {
        props["wineVersion"] = wine;
    }
    props["cpuCores"] = CpuCores();
    props["ramMb"] = TotalRamMb();

    // Active feature tags, backend-agnostic: the backend contributes its own
    // (launch modes), the framework adds its framework-level toggles. Emitted as
    // a sorted, comma-joined string because Aptabase props are scalar - it
    // groups and "contains"-filters cleanly in the dashboard.
    std::vector<std::string> features = game::GetActiveFeatures();
    const auto& appConfig = config::GetAppConfig();
    if (appConfig.inspectorPort.load() > 0) {
        features.emplace_back("inspector");
    }
    if (std::fabs(appConfig.speed.load() - 1.0F) > SPEED_EPSILON) {
        features.emplace_back("speedhack");
    }
    if (appConfig.waitForProfile.load()) {
        features.emplace_back("waitForProfile");
    }
    if (appConfig.enableUnsupported.load()) {
        features.emplace_back("unsupported");
    }
    if (appConfig.v8SingleThreadedPlatform) {
        features.emplace_back("v8SingleThreaded");
    }
    std::ranges::sort(features);
    props["features"] = Join(features, ',');

    props["compatOverrides"] = Join(CompatibilityOverrides(), ',');

    return PostEvent({.host = host_, .sessionId = sessionId_, .installId = installId_}, logger_, "session_start",
                     std::move(props));
}

bool Analytics::SendProfileEvent(const std::string& profileHash) {
    json props;
    props["profileHash"] = profileHash;
    return PostEvent({.host = host_, .sessionId = sessionId_, .installId = installId_}, logger_, "profile_active",
                     std::move(props));
}

}  // namespace d2bs::js::analytics
