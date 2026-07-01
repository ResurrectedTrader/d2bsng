#include "components/update/UpdateChecker.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

// The V8-free HTTP engine (WinHTTP; bypasses the game's SOCKS5 detour). Reused
// here rather than duplicating the WinHTTP plumbing - this is a documented
// components -> api exception (HttpEngine.h pulls in no V8 / JS headers).
#include "api/classes/io/HttpEngine.h"
#include "config/Version.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::js::update {

namespace {

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann's conventional alias spelling

constexpr auto CHECK_INTERVAL = std::chrono::hours{6};
// Short settle delay before the first check so it doesn't pile onto the heavy
// DLL-load / framework-init work.
constexpr auto INITIAL_DELAY = std::chrono::seconds{5};

// Hardcoded: the canonical d2bsng releases endpoint. `releases/latest` returns
// the newest non-draft, non-prerelease release (404 when none exist yet).
constexpr std::string_view RELEASES_API_URL = "https://api.github.com/repos/ResurrectedTrader/d2bsng/releases/latest";

// Drop a single leading 'v' / 'V' version-tag prefix (GitHub tags read "v2.1.0";
// the value we compare and display is "2.1.0"). Returns a view into `tag`.
std::string_view StripTagPrefix(std::string_view tag) {
    if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
        tag.remove_prefix(1);
    }
    return tag;
}

// Parse the leading "major.minor.patch" of a version string. Tolerates a
// leading 'v' and ignores any pre-release / build suffix ("-dev", "+meta"), so
// "v2.1.0" and "2.1.0-dev" both parse. Missing trailing components default to
// 0. Returns nullopt only when there is no leading numeric component at all.
std::optional<SemVer> ParseSemVer(std::string_view s) {
    s = StripTagPrefix(s);
    // Keep only the leading run of digits and dots ("2.1.0-dev" -> "2.1.0").
    size_t end = 0;
    while (end < s.size() && ((s[end] >= '0' && s[end] <= '9') || s[end] == '.')) {
        ++end;
    }
    s = s.substr(0, end);
    if (s.empty() || s.front() < '0' || s.front() > '9') {
        return std::nullopt;
    }

    SemVer out;
    size_t field = 0;
    size_t start = 0;
    while (start <= s.size() && field < 3) {
        const size_t dot = s.find('.', start);
        const size_t len = (dot == std::string_view::npos) ? std::string_view::npos : dot - start;
        const std::string_view part = s.substr(start, len);
        uint32_t value = 0;
        if (!part.empty()) {
            const auto* first = part.data();
            const auto* last = part.data() + part.size();
            if (std::from_chars(first, last, value).ec != std::errc{}) {
                return std::nullopt;
            }
        }
        switch (field) {
            case 0:
                out.major = value;
                break;
            case 1:
                out.minor = value;
                break;
            default:
                out.patch = value;
                break;
        }
        ++field;
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return out;
}

// True if `version` carries anything beyond a numeric "major[.minor[.patch]]"
// core - a pre-release ("-dev") or build-metadata ("+sha") suffix. Such builds
// are not released versions, so they opt out of update checking entirely.
bool HasVersionSuffix(std::string_view version) {
    version = StripTagPrefix(version);
    return std::ranges::any_of(version, [](char c) { return (c < '0' || c > '9') && c != '.'; });
}

}  // namespace

UpdateChecker& UpdateChecker::Instance() {
    static UpdateChecker instance;
    return instance;
}

UpdateChecker::UpdateChecker() {
    logger_ = utils::GetLogger("update");
}

UpdateChecker::~UpdateChecker() {
    Stop();
}

void UpdateChecker::Start() {
    // Pre-release / dev builds (D2BS_VERSION carries a -suffix or +metadata, e.g.
    // the "2.0.0-dev" fallback) are not released versions, so they opt out of
    // update checking entirely - no poll thread, no network traffic.
    if (HasVersionSuffix(D2BS_VERSION)) {
        logger_->debug("update check disabled for non-release version {}", D2BS_VERSION);
        return;
    }
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;  // already running
    }
    thread_ = std::jthread([this](const std::stop_token& stopToken) { Run(stopToken); });
}

void UpdateChecker::Stop() {
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

void UpdateChecker::Run(const std::stop_token& stopToken) {
    thread_utils::SetThreadDescription("d2bs update checker");

    std::unique_lock lock(mutex_);
    // Interruptible settle delay before the first check. wait_for returns the
    // predicate result, so a true return means Stop() fired during the delay.
    if (cv_.wait_for(lock, stopToken, INITIAL_DELAY, [&stopToken] { return stopToken.stop_requested(); })) {
        return;
    }

    while (!stopToken.stop_requested()) {
        lock.unlock();
        CheckOnce();
        lock.lock();
        // Sleep the interval; wakes early when Stop() requests it.
        cv_.wait_for(lock, stopToken, CHECK_INTERVAL, [&stopToken] { return stopToken.stop_requested(); });
    }
}

bool UpdateChecker::CheckOnce() {
    api::classes::HttpRequest request;
    request.method = "GET";
    request.url = std::string(RELEASES_API_URL);
    request.headers = {
        // GitHub rejects API requests without a User-Agent.
        {"User-Agent", "d2bsng-update-checker"},
        {"Accept", "application/vnd.github+json"},
        {"X-GitHub-Api-Version", "2022-11-28"},
    };
    request.timeoutMs = 10000;
    request.totalTimeoutMs = 15000;

    api::classes::HttpResponse response;
    const std::string error = api::classes::PerformHttpRequest(request, response);
    if (!error.empty()) {
        logger_->debug("update check: request failed ({})", error);
        return false;
    }
    if (response.status != 200) {
        // 404 = no releases published yet; anything else = transient. Either
        // way there's nothing to flag.
        logger_->debug("update check: HTTP {}", response.status);
        return false;
    }

    const json doc =
        json::parse(response.body.begin(), response.body.end(), /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) {
        logger_->debug("update check: response was not a JSON object");
        return false;
    }
    const auto it = doc.find("tag_name");
    if (it == doc.end() || !it->is_string()) {
        logger_->debug("update check: no tag_name in response");
        return false;
    }
    const auto tag = it->get<std::string>();

    const auto latest = ParseSemVer(tag);
    const auto current = ParseSemVer(D2BS_VERSION);
    if (!latest) {
        logger_->debug("update check: unparseable release tag '{}'", tag);
        return false;
    }
    if (!current) {
        // D2BS_VERSION is a build-baked literal; this should never happen.
        logger_->debug("update check: unparseable running version '{}'", D2BS_VERSION);
        return false;
    }

    const bool newer = *latest > *current;
    if (newer) {
        logger_->info("update available: {} (running {})", tag, D2BS_VERSION);
    } else {
        logger_->debug("up to date: latest {} vs running {}", tag, D2BS_VERSION);
    }
    availableVersion_.store(newer ? *latest : SemVer{}, std::memory_order_release);
    return true;
}

}  // namespace d2bs::js::update
