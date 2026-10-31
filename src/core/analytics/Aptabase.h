#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// NOLINTBEGIN(readability-identifier-naming) - spdlog::logger is upstream API naming
namespace spdlog {
class logger;
}  // namespace spdlog
// NOLINTEND(readability-identifier-naming)

// A small Aptabase client: the event envelope Aptabase's ingest API expects and
// the HTTPS POST that delivers it. It decides nothing about *what* is reported -
// that is the caller's (the frontend's Analytics component, or an embedding
// host's own analytics under its own app key). See docs/analytics.md.

namespace d2bs::core::analytics {

// The ingest base URL for an Aptabase app key ("A-<REGION>-<digits>"): US and EU
// are derived from the region. `hostOverrideVariable`, when set in the
// environment, wins (needed for self-hosted "A-SH-" / dev "A-DEV-" keys); a
// trailing slash is dropped. Empty when the key cannot be routed.
std::string ResolveHost(std::string_view appKey, const wchar_t* hostOverrideVariable);

// True Windows version (GetVersionExW lies without an app manifest; the ntdll
// RtlGetVersion export reports the real build). "10.0.26100" style.
std::string GetOsVersion();

// User's locale as a BCP-47 tag, e.g. "en-US"; empty on failure.
std::string GetLocale();

// ISO-8601 UTC timestamp with millisecond precision, e.g. "2026-07-24T12:34:56.789Z".
std::string IsoNow();

// True when D2BS_VERSION carries a pre-release / build suffix (e.g. "2.0.0-dev").
// Such builds report with isDebug=true so Aptabase buckets them apart from
// released-version traffic rather than dropping them.
bool IsPreReleaseBuild();

// Who reports, and where to. The version is the build's D2BS_VERSION.
struct ClientSettings {
    std::string appKey;
    std::string host;           // ingest base URL (ResolveHost)
    std::string sdkName;        // systemProps.sdkVersion is "<sdkName>@<version>"
    std::string userAgentName;  // the User-Agent header is "<userAgentName>/<version>"
    uint32_t networkTimeoutMs = 10000;
    uint32_t totalTimeoutMs = 15000;
};

// One event as Aptabase's single-event ingest takes it: the event name, the
// session, the system properties of this build and machine, and `props` with
// the install id added.
nlohmann::json BuildEvent(const ClientSettings& settings, std::string_view sessionId, std::string_view installId,
                          std::string_view eventName, nlohmann::json props, std::string timestamp);

class Client {
   public:
    // `logger` receives one debug line per event sent; spdlog's default logger
    // when null.
    Client(ClientSettings settings, std::shared_ptr<spdlog::logger> logger);

    // Derive the install id and draw a new session id. Call once, off the game
    // thread (it reads the registry and the volume), before the first Send.
    void BeginSession();

    // POST one event, blocking until it is answered or times out. True on a 2xx
    // response.
    bool Send(std::string_view eventName, nlohmann::json props) const;

    [[nodiscard]] const std::string& InstallId() const { return installId_; }
    [[nodiscard]] const std::string& SessionId() const { return sessionId_; }

   private:
    ClientSettings settings_;
    std::shared_ptr<spdlog::logger> logger_;
    std::string installId_;
    std::string sessionId_;
};

}  // namespace d2bs::core::analytics
