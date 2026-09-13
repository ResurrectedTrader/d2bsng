#pragma once

#include "game/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct D2UnitStrc;

// PlugY (The Survival Kit) multi-page stash bookkeeping. Not game structs: PlugY
// grows the game's D2PlayerDataStrc allocation by sizeof(PYPlayerData) and keeps
// its page lists in that tail. Layout from PlugY/playerCustomData.h, identical
// across every 1.14d-capable release (12.00 - 14.03). Sizes verified by
// static_assert; the member functions add no data and no vtable. Members whose
// bodies need the game imports are defined in game/PlugY.cpp.
// NOLINTBEGIN(readability-identifier-naming) - struct fields match PlugY's source
namespace d2bs::imports::extras::plugy {

// PlugY's client -> server channel: the vanilla 0x3A "spend stat point" packet
// (BYTE id, WORD param) carrying an out-of-range command in the low byte
// (PlugY/Commons/updatingConst.h). The server answers each with a 0x9D page
// update that the client applies to its mirror.
constexpr uint8_t PACKET_SPEND_STAT_POINT = 0x3A;
constexpr uint8_t CMD_SELECT_PREVIOUS = 0x19;
constexpr uint8_t CMD_SELECT_NEXT = 0x1A;
constexpr uint8_t CMD_SELECT_PERSONAL = 0x1B;  // first personal page
constexpr uint8_t CMD_SELECT_SHARED = 0x1C;    // first shared page
constexpr uint8_t CMD_SELECT_FIRST = 0x1F;     // first page of the current kind
constexpr uint8_t CMD_PUT_GOLD = 0x26;         // carried gold -> shared pool (all that fits)
constexpr uint8_t CMD_TAKE_GOLD = 0x27;        // shared pool -> carried gold (all that fits)

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
    // text box and stored in the ANSI code page.
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

    // The active page's items are the stash-located items of the player's
    // inventory; an inactive page's items hang off its own list. Both chains are
    // linked through the item's pNextItem, which is what NextItem follows.
    D2UnitStrc* FirstItem(const Stash& page) const;
    static D2UnitStrc* NextItem(D2UnitStrc* item);
    static bool IsStashItem(const D2UnitStrc* item);

    template <typename Fn>
    void ForEachItem(const Stash& page, const Fn& fn) const {
        const bool isActive = IsActivePage(page);
        for (auto* item = FirstItem(page); item != nullptr; item = NextItem(item)) {
            if (isActive && !IsStashItem(item)) {
                continue;
            }
            fn(item);
        }
    }

    // The 0x3A commands that walk the server from `from` to `to`. PlugY only has
    // relative moves, so a kind change lands on that kind's first page and the rest
    // is singles (or a jump to the first page when that is shorter). The target must
    // already exist in this mirror: "next" past the last page creates a page
    // server-side, and a script bug must not mint pages. Empty for an unknown target.
    std::vector<uint8_t> PlanSwitch(PageRef from, PageRef to) const;
};
static_assert(sizeof(PYPlayerData) == 0x1C);

}  // namespace d2bs::imports::extras::plugy
// NOLINTEND(readability-identifier-naming)
