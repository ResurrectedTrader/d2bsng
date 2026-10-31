#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

// NOLINTBEGIN(readability-identifier-naming) - spdlog::logger is upstream API naming
namespace spdlog {
class logger;
}  // namespace spdlog
// NOLINTEND(readability-identifier-naming)

// The Aptabase event post and the anonymous ids it carries, shared by every
// program of ours that reports under its own app key. See docs/analytics.md.
namespace d2bs::core::analytics {

std::string NewRandomId();
std::string DeriveInstallId();
std::string HashProfileName(std::string_view installId, std::string_view profileName);

struct EventContext {
    std::string_view host;
    std::string_view appKey;
    // systemProps.sdkVersion is "<sdkName>@<version>", the User-Agent "<sdkName>-analytics/<version>".
    std::string_view sdkName;
    std::string_view sessionId;
    std::string_view installId;
};

// Returns true on a 2xx response.
bool PostEvent(const EventContext& ctx, const std::shared_ptr<spdlog::logger>& logger, std::string_view eventName,
               nlohmann::json props);

}  // namespace d2bs::core::analytics
