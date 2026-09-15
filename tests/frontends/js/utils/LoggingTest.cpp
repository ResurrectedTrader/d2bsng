#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "utils/utils.h"

namespace {

using d2bs::utils::GetLogger;
using d2bs::utils::Loggers;
using d2bs::utils::SetLogLevel;

// The spdlog registry is process-wide and shared with everything else in this
// binary, so each case works under a name nothing else uses and asserts on what
// it put there rather than on the whole list.
std::string Unique(std::string_view suffix) {
    return "test.logging." + std::string{suffix};
}

const d2bs::utils::LoggerInfo* Find(const std::vector<d2bs::utils::LoggerInfo>& loggers, const std::string& name) {
    const auto entry = std::ranges::find(loggers, name, &d2bs::utils::LoggerInfo::name);
    return entry == loggers.end() ? nullptr : &*entry;
}

}  // namespace

TEST_CASE("GetLogger hands back the same logger for a name") {
    const auto name = Unique("same");
    CHECK(GetLogger(name) == GetLogger(name));
}

TEST_CASE("A logger with no level set emits at spdlog's default") {
    // The whole point of the named-logger layer is that it does not raise the
    // volume of anything on its own.
    CHECK(GetLogger(Unique("default"))->level() == spdlog::level::info);
}

TEST_CASE("SetLogLevel applies to a logger that already exists") {
    const auto name = Unique("existing");
    const auto logger = GetLogger(name);
    REQUIRE(logger->level() == spdlog::level::info);

    SetLogLevel(name, spdlog::level::warn);
    CHECK(logger->level() == spdlog::level::warn);
}

TEST_CASE("SetLogLevel is remembered for a logger created afterwards") {
    const auto name = Unique("deferred");
    SetLogLevel(name, spdlog::level::err);
    CHECK(GetLogger(name)->level() == spdlog::level::err);
}

TEST_CASE("SetLogLevel matches the name exactly, not as a prefix") {
    const auto parent = Unique("exact");
    const auto child = parent + ".child";
    const auto parentLogger = GetLogger(parent);
    const auto childLogger = GetLogger(child);

    SetLogLevel(parent, spdlog::level::critical);
    CHECK(parentLogger->level() == spdlog::level::critical);
    CHECK(childLogger->level() == spdlog::level::info);
}

TEST_CASE("Loggers reports a created logger and the level it is emitting at") {
    const auto name = Unique("listed");
    GetLogger(name);
    SetLogLevel(name, spdlog::level::debug);

    const auto listed = Loggers();
    const auto* entry = Find(listed, name);
    REQUIRE(entry != nullptr);
    CHECK(entry->level == spdlog::level::debug);
}

TEST_CASE("Loggers grows as loggers are created") {
    const auto name = Unique("growth");
    CHECK(Find(Loggers(), name) == nullptr);
    GetLogger(name);
    CHECK(Find(Loggers(), name) != nullptr);
}

TEST_CASE("Loggers leaves out the registry's unnamed default logger") {
    // spdlog registers one under an empty name and never removes it, even after
    // set_default_logger; it does not share our sink and is not ours to report.
    const auto listed = Loggers();
    CHECK(std::ranges::none_of(listed, [](const d2bs::utils::LoggerInfo& info) { return info.name.empty(); }));
}

TEST_CASE("Loggers is sorted by name") {
    GetLogger(Unique("sorted.b"));
    GetLogger(Unique("sorted.a"));

    const auto listed = Loggers();
    CHECK(std::ranges::is_sorted(listed, {}, &d2bs::utils::LoggerInfo::name));
}
