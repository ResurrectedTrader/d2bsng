#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "utils/utils.h"

namespace {

using d2bs::utils::GetLogger;

// The spdlog registry is process-wide and shared with everything else in this
// binary, so each case works under a name nothing else uses and asserts on what
// it put there rather than on the whole list.
std::string Unique(std::string_view suffix) {
    return "test.logging." + std::string{suffix};
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
