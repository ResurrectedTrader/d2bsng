#include <doctest/doctest.h>

#include "config/AppConfig.h"

using d2bs::core::config::HostListContains;

TEST_CASE("HostListContains matches whole entries, ignoring case") {
    constexpr const char* HOSTS = "example.invalid, 127.0.0.1;API.Example.com\tlocalhost";
    CHECK(HostListContains(HOSTS, "127.0.0.1"));
    CHECK(HostListContains(HOSTS, "api.example.com"));
    CHECK(HostListContains(HOSTS, "LOCALHOST"));
    CHECK(HostListContains(HOSTS, "example.invalid"));

    // A part of an entry, or more than one, is not an entry.
    CHECK_FALSE(HostListContains(HOSTS, "127.0.0"));
    CHECK_FALSE(HostListContains(HOSTS, "example"));
    CHECK_FALSE(HostListContains(HOSTS, "127.0.0.1;API.Example.com"));
    CHECK_FALSE(HostListContains(HOSTS, ""));
}

TEST_CASE("HostListContains refuses every host for an empty list") {
    CHECK_FALSE(HostListContains("", "127.0.0.1"));
    CHECK_FALSE(HostListContains(" ,; ", "127.0.0.1"));
}
