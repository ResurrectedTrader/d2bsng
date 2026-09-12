// Guards the character-capture serialisation: the wire document UnitToJson produces, and
// the invariant that the streaming fingerprint (ContainerHash / UnitHash) covers exactly the
// structural fields the payload sends. Both go through the one VisitUnit traversal, so a
// field added there must show up in the json AND move the fingerprint - these tests fail if
// a change breaks either half of that.

#include <cstdint>
#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "components/characterstate/Fingerprint.h"
#include "components/characterstate/UnitJson.h"
#include "fakes/GameLoopCollaborators.h"
#include "fakes/UnitStore.h"
#include "game/Types.h"
#include "game/Unit.h"

namespace {

using d2bs::game::ItemLocation;
using d2bs::game::ItemQuality;
using d2bs::game::Size;
using d2bs::game::Unit;
using d2bs::js::characterstate::ContainerHash;
using d2bs::js::characterstate::Detail;
using d2bs::js::characterstate::UnitHash;
using d2bs::js::characterstate::UnitToJson;
// NOLINTNEXTLINE(readability-identifier-naming) - 'json' is nlohmann's conventional alias spelling
using json = nlohmann::json;

// A ring with one socketed gem and one stat list, distinctive values throughout.
d2bs::test::FakeItem MakeRing() {
    d2bs::test::FakeItem ring;
    ring.classId = 521;
    ring.code = "rin ";  // trailing pad, must be trimmed on the wire
    ring.quality = ItemQuality::Magic;
    ring.itemFlags = 0x5;
    ring.format = 1;
    ring.fileIndex = 42;
    ring.rarePrefix = 3;
    ring.rareSuffix = 4;
    ring.autoAffix = 5;
    ring.prefixes = {10, 20, 0};
    ring.suffixes = {30, 0, 0};
    ring.itemLevel = 77;
    ring.earLevel = 0;
    ring.playerName = "";
    ring.gfxIndex = 2;
    ring.location = ItemLocation::Inventory;
    ring.pos = {.x = 3, .y = 4};
    ring.size = {.width = 1, .height = 2};
    ring.name = "Ring of Testing";
    ring.description = "top line\nbottom line";  // game order; reversed on the wire
    d2bs::game::StatListEntry list;
    list.stateNo = 0;
    list.flags = 0x40;
    list.stats.push_back({.statId = 0, .subIndex = 0, .value = 100});  // id 0 = strength, not fixed-point
    ring.statLists.push_back(list);
    return ring;
}

Unit OnlyInventoryItem() {
    for (const auto& item : Unit::Player().GetItems()) {
        if (item.ItemLocation() == ItemLocation::Inventory) {
            return item;
        }
    }
    FAIL("no inventory item in store");
    return Unit{};
}

}  // namespace

TEST_CASE("UnitToJson emits the item wire document") {
    d2bs::test::Reset();
    d2bs::test::ResetUnits();
    d2bs::test::State().playerId = 1;
    auto& store = d2bs::test::Units();
    const uint32_t ringId = store.Add(MakeRing());
    d2bs::test::FakeItem gem;
    gem.classId = 700;
    gem.code = "gpr";
    gem.owner = ringId;
    gem.location = ItemLocation::Inventory;
    store.Add(gem);
    store.Seal();

    const json doc = UnitToJson(OnlyInventoryItem(), Detail::Full);

    CHECK(doc["unitType"] == static_cast<int>(d2bs::game::UnitType::Item));
    CHECK(doc["classId"] == 521);
    CHECK(doc["code"] == "rin");  // trimmed
    CHECK(doc["quality"] == static_cast<int>(ItemQuality::Magic));
    CHECK(doc["itemFlags"] == 0x5);
    CHECK(doc["fileIndex"] == 42);
    CHECK(doc["magicPrefix"] == json::array({10, 20, 0}));
    CHECK(doc["magicSuffix"] == json::array({30, 0, 0}));
    CHECK(doc["itemLevel"] == 77);
    CHECK(doc["x"] == 3);
    CHECK(doc["y"] == 4);
    CHECK(doc["w"] == 1);
    CHECK(doc["h"] == 2);

    SUBCASE("Full detail carries the presentation fields") {
        CHECK(doc["title"] == "Ring of Testing");
        CHECK(doc["description"] == "bottom line\ntop line");  // reversed to display order
        REQUIRE(doc["statsLists"].is_array());
        REQUIRE(doc["statsLists"].size() == 1);
        CHECK(doc["statsLists"][0]["flags"] == 0x40);
        CHECK(doc["statsLists"][0]["stats"][0]["id"] == 0);
        CHECK(doc["statsLists"][0]["stats"][0]["value"] == 100);
    }

    SUBCASE("sockets recurse positionally") {
        REQUIRE(doc["sockets"].is_array());
        REQUIRE(doc["sockets"].size() == 1);
        CHECK(doc["sockets"][0]["code"] == "gpr");
        CHECK(doc["sockets"][0]["classId"] == 700);
    }
}

TEST_CASE("Structural detail drops the Full-only fields") {
    d2bs::test::Reset();
    d2bs::test::ResetUnits();
    d2bs::test::State().playerId = 1;
    d2bs::test::Units().Add(MakeRing());
    d2bs::test::Units().Seal();

    const json doc = UnitToJson(OnlyInventoryItem(), Detail::Structural);

    CHECK(doc.contains("code"));
    CHECK_FALSE(doc.contains("title"));
    CHECK_FALSE(doc.contains("description"));
    CHECK_FALSE(doc.contains("statsLists"));
}

TEST_CASE("ContainerHash moves for every structural field, not for a Full-only one") {
    d2bs::test::Reset();
    d2bs::test::ResetUnits();
    d2bs::test::State().playerId = 1;
    const uint32_t ringId = d2bs::test::Units().Add(MakeRing());
    d2bs::test::Units().Seal();

    const Size dims{.width = 10, .height = 4};
    const std::vector<Unit> items = {OnlyInventoryItem()};
    const size_t base = ContainerHash(items, dims);

    auto& stored = d2bs::test::Units().items.at(ringId);

    SUBCASE("a structural field flips the hash") {
        stored.itemFlags ^= 1U;
        CHECK(ContainerHash(items, dims) != base);
        stored.itemFlags ^= 1U;
        CHECK(ContainerHash(items, dims) == base);  // and back

        stored.pos.x += 1;
        CHECK(ContainerHash(items, dims) != base);
        stored.pos.x -= 1;

        stored.quality = ItemQuality::Rare;
        CHECK(ContainerHash(items, dims) != base);
        stored.quality = ItemQuality::Magic;

        stored.prefixes[2] = 99;
        CHECK(ContainerHash(items, dims) != base);
        stored.prefixes[2] = 0;

        CHECK(ContainerHash(items, dims) == base);  // every mutation restored
    }

    SUBCASE("a Full-only field does not, because the fingerprint is Structural") {
        // statsLists is Full-only - it carries live durability/quantity, deliberately kept
        // out of the fingerprint so a ticking counter does not force a resend.
        stored.statLists[0].stats[0].value = 101;
        CHECK(ContainerHash(items, dims) == base);
        stored.description = "totally different tooltip";
        CHECK(ContainerHash(items, dims) == base);
        stored.name = "Renamed";
        CHECK(ContainerHash(items, dims) == base);
    }

    SUBCASE("the declared grid is part of the hash") {
        CHECK(ContainerHash(items, Size{.width = 4, .height = 1}) != base);
    }
}

TEST_CASE("UnitHash tracks a wearer's identity and skills") {
    d2bs::test::Reset();
    d2bs::test::ResetUnits();
    d2bs::test::State().playerId = 1;
    auto& player = d2bs::test::Units().player;
    player.name = "Sorc";
    player.skills.push_back({.skillId = 36, .baseLevel = 1, .totalLevel = 5});
    d2bs::test::Units().Seal();

    const size_t base = UnitHash(Unit::Player());

    player.skills[0].totalLevel = 6;
    CHECK(UnitHash(Unit::Player()) != base);
    player.skills[0].totalLevel = 5;
    CHECK(UnitHash(Unit::Player()) == base);

    player.name = "Necro";
    CHECK(UnitHash(Unit::Player()) != base);
}
