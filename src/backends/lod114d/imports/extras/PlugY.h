#pragma once

#include "game/Types.h"

#include <cstdint>
#include <optional>
#include <string>

struct D2UnitStrc;

// PlugY (The Survival Kit) multi-page stash bookkeeping. Not game structs: PlugY
// grows the game's D2PlayerDataStrc allocation by sizeof(PYPlayerData) and keeps
// its page lists in that tail. Layout from PlugY/playerCustomData.h, identical
// across every 1.14d-capable release (12.00 - 14.03). Sizes verified by
// static_assert; the member functions add no data and no vtable.
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

    // User-given name as UTF-8; empty when unnamed. Typed into PlugY's in-game
    // text box and stored in the ANSI code page. Defined in game/PlugY.cpp.
    std::string Name() const;
};
static_assert(sizeof(Stash) == 0x18);

// A page by position, the identity a game::StashTab carries.
struct PageRef {
    game::StashTabKind kind = game::StashTabKind::Personal;
    uint32_t index = 0;

    bool operator==(const PageRef&) const = default;
};

struct PYPlayerData {
    uint32_t flags;  // bit0 selfStashIsOpened, bit1 sharedStashIsOpened, bit2 showSharedStash
    uint32_t sharedGold;
    uint32_t nbSelfPages;
    uint32_t nbSharedPages;
    Stash* currentStash;  // null until the page lists are populated (never on Battle.net)
    Stash* selfStash;     // head of the personal page list
    Stash* sharedStash;   // head of the shared page list; null when ActiveSharedStash is off

    // Guard for a corrupted or cyclic page list; PlugY itself has no practical limit.
    static constexpr uint32_t MAX_PAGES_WALKED = 1U << 16;

    const Stash* Head(game::StashTabKind kind) const {
        return kind == game::StashTabKind::Shared ? sharedStash : selfStash;
    }

    bool IsActivePage(const Stash& page) const { return &page == currentStash; }

    // Walks `kind`'s pages in order, calling fn(page, index) until it returns true;
    // returns the page it stopped on, or nullptr.
    template <typename Fn>
    const Stash* ForEachPage(game::StashTabKind kind, const Fn& fn) const {
        uint32_t index = 0;
        for (const Stash* page = Head(kind); page != nullptr && index < MAX_PAGES_WALKED;
             page = page->nextStash, ++index) {
            if (fn(*page, index)) {
                return page;
            }
        }
        return nullptr;
    }

    const Stash* FindPage(game::StashTabKind kind, uint32_t index) const {
        return ForEachPage(kind, [index](const Stash&, uint32_t i) { return i == index; });
    }

    uint32_t PageCount(game::StashTabKind kind) const {
        uint32_t count = 0;
        ForEachPage(kind, [&count](const Stash&, uint32_t) {
            ++count;
            return false;
        });
        return count;
    }

    // The page currentStash points at, by position; nullopt when it is on neither list.
    std::optional<PageRef> ActivePage() const {
        for (const auto kind : {game::StashTabKind::Personal, game::StashTabKind::Shared}) {
            uint32_t found = 0;
            if (ForEachPage(kind, [&](const Stash& page, uint32_t i) {
                    found = i;
                    return IsActivePage(page);
                }) != nullptr) {
                return PageRef{.kind = kind, .index = found};
            }
        }
        return std::nullopt;
    }
};
static_assert(sizeof(PYPlayerData) == 0x1C);

}  // namespace d2bs::imports::extras::plugy
// NOLINTEND(readability-identifier-naming)
