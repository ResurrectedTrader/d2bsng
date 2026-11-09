#include <doctest/doctest.h>
#include <array>
#include <cstring>
#include <format>
#include <string>
#include "../../../../src/backends/lod114d/hooks/RevisionTempPath.h"

TEST_CASE("Revision extraction never accepts a truncated or missing destination") {
    using d2bs::lod114d::hooks::GetRevisionTempPath;
    CHECK(GetRevisionTempPath(0, nullptr) == 0);
    std::array<char, 4> tinyBuffer = {'x', 'x', 'x', 'x'};
    CHECK(GetRevisionTempPath(static_cast<DWORD>(tinyBuffer.size()), tinyBuffer.data()) == 0);
    CHECK(tinyBuffer[0] == '\0');
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
}

TEST_CASE("Revision extraction directory identifies its process and rejects an exact-fit buffer") {
    using d2bs::lod114d::hooks::GetRevisionTempPath;
    std::array<char, MAX_PATH> path{};
    const DWORD length = GetRevisionTempPath(static_cast<DWORD>(path.size()), path.data());
    REQUIRE(length != 0);
    CHECK(length == std::strlen(path.data()));
    CHECK(std::string{path.data()}.ends_with(std::format("d2bs-revision-{}\\", GetCurrentProcessId())));
    CHECK((GetFileAttributesA(path.data()) & FILE_ATTRIBUTE_DIRECTORY) != 0);
    std::array<char, MAX_PATH> shortPath{};
    CHECK(GetRevisionTempPath(length, shortPath.data()) == 0);
    CHECK(shortPath[0] == '\0');
    CHECK(GetRevisionTempPath(length + 1, shortPath.data()) == length);
    CHECK(std::strcmp(path.data(), shortPath.data()) == 0);
    CHECK(RemoveDirectoryA(path.data()) != 0);
}
