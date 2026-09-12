#pragma once

// Item and wearer state behind the Unit fake for characterstate tests. A unit is a
// handle keyed by id: 0 is the player, anything else an item. An item's socket fillers
// are its children and the player's items are the top-level list; Seal() resolves the
// next-links so GetNextItem is a lookup rather than a scan.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/Types.h"
#include "game/Unit.h"

namespace d2bs::test {

struct FakeItem {
    uint32_t id = 0;
    uint32_t owner = 0;  // 0 = player, else the item this is socketed into
    uint32_t next = 0;   // next sibling under the same owner, 0 at the end
    uint32_t classId = 0;
    std::string code;
    d2bs::game::ItemQuality quality = d2bs::game::ItemQuality::Normal;
    uint32_t itemFlags = 0;
    uint16_t format = 0;
    std::optional<uint32_t> fileIndex;
    uint16_t rarePrefix = 0;
    uint16_t rareSuffix = 0;
    uint16_t autoAffix = 0;
    std::array<uint16_t, 3> prefixes{};
    std::array<uint16_t, 3> suffixes{};
    uint32_t itemLevel = 0;
    uint32_t earLevel = 0;
    std::string playerName;
    uint32_t gfxIndex = 0;
    d2bs::game::ItemLocation location = d2bs::game::ItemLocation::Inventory;
    d2bs::game::Position pos;
    d2bs::game::Size size{.width = 1, .height = 1};
    // Read only at Detail::Full.
    std::string name;
    std::string description;
    std::vector<d2bs::game::StatListEntry> statLists;
    std::vector<uint32_t> children;
};

struct FakeWearer {
    uint32_t classId = 1;
    uint32_t flagsEx = 0;
    std::string name;
    uint32_t weaponSwitch = 0;
    std::vector<d2bs::game::Unit::SkillInfo> skills;
    std::unordered_map<uint32_t, int32_t> stats;
    std::vector<uint32_t> items;  // top-level, in chain order
};

struct UnitStore {
    FakeWearer player;
    std::unordered_map<uint32_t, FakeItem> items;

    // Registers an item, assigning an id if it has none, and appends it to its owner's
    // list. A filler's owner must already be registered.
    uint32_t Add(FakeItem item);
    // Resolves next-links from the lists. Call once after the last Add.
    void Seal();
    [[nodiscard]] const FakeItem* Find(uint32_t id) const;
};

UnitStore& Units();
void ResetUnits();

}  // namespace d2bs::test
