#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>

#include "game/Types.h"
#include "utils/EnumNaming.h"

namespace d2bs::game {

TEST_CASE("EnumName - an enumerator's own name") {
    CHECK(EnumName(UnitType::Monster) == "Monster");
    CHECK(EnumName(UnitType::Player) == "Player");
}

TEST_CASE("EnumName - single-bit enumerators that make up the value exactly") {
    CHECK(EnumName(static_cast<MonsterSpecType>(0x0A)) == "Champion|Minion");
    CHECK(EnumName(static_cast<MonsterSpecType>(0x0F)) == "SuperUnique|Champion|Unique|Minion");
}

TEST_CASE("EnumName - Type(value) when no enumerator names it") {
    CHECK(EnumName(static_cast<MonsterSpecType>(0x10)) == "MonsterSpecType(16)");
    CHECK(EnumName(static_cast<MonsterSpecType>(0x12)) == "MonsterSpecType(18)");
}

TEST_CASE("EnumName - std::format prints the name and applies string specs") {
    CHECK(std::format("{}", UnitType::Item) == "Item");
    CHECK(std::format("[{:>6}]", UnitType::Item) == "[  Item]");
    CHECK(std::format("{}", static_cast<MonsterSpecType>(0x03)) == "SuperUnique|Champion");
}

}  // namespace d2bs::game

namespace d2bs::utils {

TEST_CASE("LookupEnumName - aliases, masks and gaps") {
    constexpr auto ENTRIES = std::to_array<EnumEntry>({
        {.bits = 0x1, .name = "A"},
        {.bits = 0x1, .name = "AliasOfA"},
        {.bits = 0x2, .name = "B"},
        {.bits = 0x3, .name = "AllMask"},
        {.bits = 0x8, .name = "D"},
    });
    CHECK(LookupEnumName(ENTRIES, 0x1) == "A");        // the first declared name wins
    CHECK(LookupEnumName(ENTRIES, 0x3) == "AllMask");  // an exact multi-bit enumerator beats the decomposition
    CHECK(LookupEnumName(ENTRIES, 0x9) == "A|D");      // masks never take part in a decomposition
    CHECK(LookupEnumName(ENTRIES, 0x4).empty());       // no enumerator covers bit 2
    CHECK(LookupEnumName(ENTRIES, 0x0).empty());
}

}  // namespace d2bs::utils
