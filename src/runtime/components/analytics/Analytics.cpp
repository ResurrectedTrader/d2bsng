#include "components/analytics/Analytics.h"

#include <Windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "analytics/Aptabase.h"
#include "analytics/InstallId.h"
#include "config/AppConfig.h"
#include "config/CompatibilityFlags.h"
#include "game/GameHelpers.h"
#include "utils/crypto.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::runtime::analytics {

namespace {

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann's conventional alias spelling
using core::analytics::GetEnv;
using core::analytics::ID_SALT;
using core::analytics::IsTruthy;

constexpr int32_t MAX_ATTEMPTS = 3;

#ifdef _WIN64
constexpr std::string_view ARCH = "x64";
#else
constexpr std::string_view ARCH = "x86";
#endif

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

// Compatibility flags that are NOT at their registered default, as `-name`
// (turned off) / `+name` (turned on), sorted. Only deviations are worth sending:
// every flag sitting at its default is the norm and would drown the signal, while
// the deviations are exactly what says whether a legacy shim is still load-bearing
// or safe to retire. Scripts are the only thing that toggles these (the
// `Compatibility` JS object), so this is the state as of the settle delay.
std::vector<std::string> CompatibilityOverrides() {
    std::vector<std::string> overrides;
    for (const auto& flag : core::config::CompatibilityFlags::Instance().All()) {
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

    std::string host = core::analytics::ResolveHost(EMBEDDED_APP_KEY, L"D2BS_ANALYTICS_HOST");
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
    client_ = std::make_unique<core::analytics::Client>(
        core::analytics::ClientSettings{.appKey = std::string(EMBEDDED_APP_KEY),
                                        .host = std::move(host),
                                        .sdkName = "d2bsng",
                                        .userAgentName = "d2bsng-analytics"},
        logger_);
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
    utils::threads::SetThreadDescription("d2bs analytics");

    // Top frame of the reporter thread, so an escaping exception is
    // std::terminate() and takes the game with it. std::random_device (session
    // id) throws when the OS exposes no entropy source, and json::dump() throws
    // on invalid UTF-8; losing the telemetry is the correct outcome for both.
    try {
        std::unique_lock lock(mutex_);
        // Short settle delay before the first send so it doesn't pile onto the heavy
        // DLL-load / framework-init work.
        constexpr auto INITIAL_DELAY = std::chrono::seconds{5};
        // Retry a failed POST a few times (the network may not be up at inject time),
        // backing off between attempts.
        constexpr auto RETRY_INTERVAL = std::chrono::seconds{30};
        if (WaitFor(lock, stopToken, INITIAL_DELAY)) {
            return;
        }

        client_->BeginSession();

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
        // How often the reporter re-reads the active profile name. A profile lives for a
        // whole bot run, so this only bounds how quickly a switch is noticed; the poll
        // itself is a string copy against a shared_mutex.
        constexpr auto PROFILE_POLL_INTERVAL = std::chrono::seconds{5};
        if (WaitFor(lock, stopToken, PROFILE_POLL_INTERVAL)) {
            return;
        }

        if (pending.empty()) {
            const std::string name = core::config::GetAppConfig().GetProfileName();
            if (name.empty()) {
                continue;  // no profile yet - a script hasn't logged in
            }
            std::string hash = HashProfileName(client_->InstallId(), name);
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
    const auto& appConfig = core::config::GetAppConfig();
    if (appConfig.inspectorPort.load() > 0) {
        features.emplace_back("inspector");
    }
    // Speeds within this of 1.0 count as "no speedhack" (the default is exactly 1.0).
    constexpr float SPEED_EPSILON = 0.0001F;
    if (std::fabs(appConfig.speed.load() - 1.0F) > SPEED_EPSILON) {
        features.emplace_back("speedhack");
    }
    if (appConfig.waitForProfile.load()) {
        features.emplace_back("waitForProfile");
    }
    if (appConfig.enableUnsupported.load()) {
        features.emplace_back("unsupported");
    }
    if (appConfig.engineSingleThreaded) {
        features.emplace_back("engineSingleThreaded");
    }
    std::ranges::sort(features);
    props["features"] = Join(features, ',');

    props["compatOverrides"] = Join(CompatibilityOverrides(), ',');

    return client_->Send("session_start", std::move(props));
}

bool Analytics::SendProfileEvent(const std::string& profileHash) {
    json props;
    props["profileHash"] = profileHash;
    return client_->Send("profile_active", std::move(props));
}

}  // namespace d2bs::runtime::analytics
