#include "analytics/Aptabase.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

#include "analytics/InstallId.h"
#include "config/Version.h"
#include "http/Client.h"
#include "utils/utils.h"

namespace d2bs::core::analytics {

using json = nlohmann::json;  // NOLINT(readability-identifier-naming) - nlohmann's conventional alias spelling

std::string ResolveHost(std::string_view appKey, const wchar_t* hostOverrideVariable) {
    if (std::string hostOverride = GetEnv(hostOverrideVariable); !hostOverride.empty()) {
        // Drop a trailing slash so the event path joins cleanly.
        while (!hostOverride.empty() && hostOverride.back() == '/') {
            hostOverride.pop_back();
        }
        return hostOverride;
    }

    // Region is the token between the first two dashes: A-EU-123 -> "EU".
    const size_t firstDash = appKey.find('-');
    if (firstDash == std::string_view::npos) {
        return {};
    }
    const size_t secondDash = appKey.find('-', firstDash + 1);
    if (secondDash == std::string_view::npos) {
        return {};
    }
    const std::string_view region = appKey.substr(firstDash + 1, secondDash - firstDash - 1);
    if (region == "US") {
        return "https://us.aptabase.com";
    }
    if (region == "EU") {
        return "https://eu.aptabase.com";
    }
    return {};
}

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

std::string GetLocale() {
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> buffer{};
    if (GetUserDefaultLocaleName(buffer.data(), buffer.size()) > 0) {
        return utils::ToStr(buffer.data(), CP_UTF8);
    }
    return {};
}

std::string IsoNow() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &seconds);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                       utc.tm_hour, utc.tm_min, utc.tm_sec, ms.count());
}

bool IsPreReleaseBuild() {
    const std::string_view version = D2BS_VERSION;
    return version.find('-') != std::string_view::npos || version.find('+') != std::string_view::npos;
}

json BuildEvent(const ClientSettings& settings, std::string_view sessionId, std::string_view installId,
                std::string_view eventName, json props, std::string timestamp) {
    json event;
    event["timestamp"] = std::move(timestamp);
    event["sessionId"] = sessionId;
    event["eventName"] = eventName;

    json systemProps;
    systemProps["isDebug"] = IsPreReleaseBuild();
    systemProps["osName"] = "Windows";
    systemProps["osVersion"] = GetOsVersion();
    systemProps["locale"] = GetLocale();
    systemProps["appVersion"] = D2BS_VERSION;
    systemProps["appBuildNumber"] = "";
    systemProps["sdkVersion"] = settings.sdkName + "@" + D2BS_VERSION;
    event["systemProps"] = std::move(systemProps);

    props["installId"] = installId;
    event["props"] = std::move(props);
    return event;
}

Client::Client(ClientSettings settings, std::shared_ptr<spdlog::logger> logger)
    : settings_(std::move(settings)), logger_(logger ? std::move(logger) : spdlog::default_logger()) {}

void Client::BeginSession() {
    installId_ = DeriveInstallId();
    sessionId_ = NewRandomId();
}

bool Client::Send(std::string_view eventName, json props) const {
    const json event = BuildEvent(settings_, sessionId_, installId_, eventName, std::move(props), IsoNow());

    http::Request request;
    request.method = "POST";
    // Aptabase's single-event ingest path.
    constexpr std::string_view EVENT_PATH = "/api/v0/event";
    request.url = settings_.host + std::string(EVENT_PATH);
    request.headers = {
        {"Content-Type", "application/json"},
        {"App-Key", settings_.appKey},
        {"User-Agent", settings_.userAgentName + "/" + D2BS_VERSION},
    };
    const std::string body = event.dump();
    request.body.assign(body.begin(), body.end());
    request.timeoutMs = settings_.networkTimeoutMs;
    request.totalTimeoutMs = settings_.totalTimeoutMs;

    http::Response response;
    const std::string error = http::Perform(request, response);
    if (!error.empty()) {
        logger_->debug("analytics: {} request failed ({})", eventName, error);
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        logger_->debug("analytics: {} HTTP {}", eventName, response.status);
        return false;
    }
    logger_->debug("analytics: {} reported", eventName);
    return true;
}

}  // namespace d2bs::core::analytics
