#include <doctest/doctest.h>

#include <ostream>
#include <string>

#include "analytics/InstallId.h"

namespace {

using d2bs::core::analytics::DeriveInstallId;
using d2bs::core::analytics::HashInstallId;
using d2bs::core::analytics::IsTruthy;
using d2bs::core::analytics::MachineFacts;
using d2bs::core::analytics::NewRandomId;
using d2bs::core::analytics::ReadMachineFacts;

}  // namespace

// The expected digests are sha256 of the material spelled out, computed outside
// the code under test (docs/analytics.md, "The installId"): another program has
// to arrive at the same bytes.
TEST_CASE("HashInstallId hashes salt|MachineGuid|serial|computerName") {
    const MachineFacts facts{.machineGuid = "6f1c2a3e-0b1d-4c5e-9a7b-112233445566",
                             .systemVolumeSerial = 0x12345678,
                             .computerName = "BOTBOX"};
    CHECK(HashInstallId(facts) == "f04e11d22a946a320b4ff35b46a63b80b9da4bed1963c391a5629c84abb949ef");
}

TEST_CASE("HashInstallId keeps every field when the facts are missing") {
    // "ResurrectedTrader-analytics-v1||0|": empty fields stay, the serial is the literal 0.
    CHECK(HashInstallId({}) == "5875b664d7d0dc75ccaf6771378fbdbddb7f89dd1ac1f7ec48c34881d6fd7b66");
}

TEST_CASE("HashInstallId hashes the UTF-8 bytes") {
    const MachineFacts facts{.machineGuid = "g", .systemVolumeSerial = 1, .computerName = "P\xc3\x84"};
    CHECK(HashInstallId(facts) == "abf302e57e0642553431daa02d38da8bb7190b960e60f3bd0877b83fd95b569e");
}

TEST_CASE("DeriveInstallId is the hash of this machine's facts") {
    const std::string id = DeriveInstallId();
    CHECK(id.size() == 64);
    CHECK(id == HashInstallId(ReadMachineFacts()));
    CHECK(id == DeriveInstallId());
}

TEST_CASE("NewRandomId is a canonical GUID string") {
    const std::string id = NewRandomId();
    REQUIRE(id.size() == 36);
    CHECK(id[8] == '-');
    CHECK(id[13] == '-');
    CHECK(id[18] == '-');
    CHECK(id[23] == '-');
    CHECK(id != NewRandomId());
}

TEST_CASE("IsTruthy is false only for empty and clear falsey values") {
    CHECK_FALSE(IsTruthy(""));
    CHECK_FALSE(IsTruthy("0"));
    CHECK_FALSE(IsTruthy("false"));
    CHECK_FALSE(IsTruthy("No"));
    CHECK(IsTruthy("1"));
    CHECK(IsTruthy("true"));
    CHECK(IsTruthy("yes"));
}
