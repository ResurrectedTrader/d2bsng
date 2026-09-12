#pragma once

#include <cstdint>

#include <Units/Units.h>  // D2UnitStrc

// PlugY (The Survival Kit) multi-page stash bookkeeping. Not game structs: PlugY
// grows the game's D2PlayerDataStrc allocation by sizeof(PYPlayerData) and keeps
// its page lists in that tail. Layout from PlugY/playerCustomData.h, identical
// across every 1.14d-capable release (12.00 - 14.03). Sizes verified by
// static_assert.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match PlugY's source
namespace d2bs::imports::extras::plugy {

struct Stash {
    uint32_t id;     // 0-based position within its list
    uint32_t flags;  // bit0 isShared, bit1 isIndex, bit2 isMainIndex, bit3 isReserved
    char* name;      // nullable; ANSI, at most 20 chars
    // Items parked off-inventory while the page is inactive, linked through
    // pItemData->pExtraData.pNextItem. Always null on the active page: its items
    // are the ones in the player's inventory.
    D2UnitStrc* ptListItem;
    Stash* previousStash;
    Stash* nextStash;
};
static_assert(sizeof(Stash) == 0x18);

struct PYPlayerData {
    uint32_t flags;  // bit0 selfStashIsOpened, bit1 sharedStashIsOpened, bit2 showSharedStash
    uint32_t sharedGold;
    uint32_t nbSelfPages;
    uint32_t nbSharedPages;
    Stash* currentStash;  // null until the page lists are populated (never on Battle.net)
    Stash* selfStash;     // head of the personal page list
    Stash* sharedStash;   // head of the shared page list; null when ActiveSharedStash is off
};
static_assert(sizeof(PYPlayerData) == 0x1C);

}  // namespace d2bs::imports::extras::plugy
// NOLINTEND(readability-identifier-naming)
