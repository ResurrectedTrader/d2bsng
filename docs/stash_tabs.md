# Stash tabs

Scripts can enumerate the tabs of the player's stash, read the items on any
tab, click into any tab, and move gold to or from a tab, whether or not the tab
is the one the panel shows. The contract is game-agnostic: vanilla 1.14d has a
single personal tab, a paged-stash mod or D2R adds more, including account-wide
shared tabs. This documents the contract, the JS surface, the gold rules and
the character-state feed. A backend that implements more than the single tab
documents its own mechanics separately (`docs/plugy_stash.md` for PlugY).

## Contract

`src/contract/game/Types.h` holds the two enums; `src/contract/game/StashTab.h`
the handle, an identity (kind + position) whose reads the backend resolves live,
like `Unit` or `Room`:

```cpp
enum class StashTabKind : uint8_t { Personal = 0, Shared = 1 };
enum class StashTabType : uint8_t { Normal = 0, AdvancedStash = 1, Chronicle = 2 };

class StashTab {
    StashTabKind Kind() const;   uint32_t Index() const;   // identity, no resolve
    explicit operator bool() const;      // the tab exists right now
    StashTabType Type() const;           // always Normal on LoD
    std::string Name() const;            // user-given, empty when unnamed
    uint32_t Gold() const;               // see Gold
    std::vector<Unit> GetItems() const;  // shown or not
    ClickResult Click(Position cell) const;          // left click: pick up / drop / swap
    bool DepositGold(uint32_t amount) const;         // fire and forget
    bool WithdrawGold(uint32_t amount) const;
};
std::vector<StashTab> GetStashTabs();    // personal first, then shared; empty with no player unit
std::optional<StashTab> Unit::StashTab() const;   // the tab holding an item
```

Every member below the identity is game-impl required, defined per port in
`game/Stash.cpp`. A handle stays valid across page changes and reads empty
(`false`, `""`, `0`, no items) once its tab is gone, so scripts can hold on to
one.

`StashTabType` mirrors D2R's tab kinds: `Normal` holds items, `AdvancedStash`
holds items with stackable-item support, `Chronicle` tracks found set / unique
/ runeword items and holds no items. Every LoD tab is `Normal`.

There is deliberately no "select tab" in the contract. On a backend whose
packets cannot address a tab that is not shown, a tab switch is a server round
trip; on D2R it is a pure UI action. Exposing it would force scripts to
sequence something only one backend needs, so `Click` owns whatever switching
is required and blocks the calling script thread until the game state is
consistent again. Scripts see one synchronous call either way, and nothing in
the API reports or changes which tab the panel shows.

Only a left click is offered for tab cells. Neither vanilla LoD nor its mods
have a modifier click that moves an item to the stash (that is a D2R addition):
shift-click is the game's quick action (belt / tome / sell) and right-click
uses a consumable, so on a stash cell only the left click does anything.
`ClickItem(button, item)` keeps its `button` for parity and reaches items on
tabs that are not shown.

Placement rules are the game's: the click drops on an empty footprint, swaps
when exactly one item is under it, and rejects the click otherwise, and the
server validates the resulting drop packet. `Dispatched` therefore means
"handed to the game", as for every other click; scripts confirm with
`GetItems()` / `Unit::StashTab()` afterwards.

The pre-existing item API (`GetItems`, `FindInvItem`, JS `getItems()` /
`getItem()`) only ever sees the inventory, which on a paged stash means the tab
that is shown. That view is left alone on purpose: legacy scripts assume
"stash items = location 7 in the inventory", and the other tabs are reached
through the tab API.

### 1.14d

One personal tab, index 0, whose `GetItems()` is the inventory walk filtered to
`ItemLocation::Stash`, whose `Gold()` is the character's stash gold, and whose
`Click` is `ClickContainerSlot(Left, cell, Stash)`. Implemented in
`backends/lod114d/game/Stash.cpp`.

## JS surface

A `StashTab` class (`api/classes/game/JSStashTab.h`) wrapping `game::StashTab`
one to one, the same shape as `Unit` or `Room`:

- `getStashTabs()` -> `StashTab[]`, personal first, then shared
- `unit.stashTab` -> `StashTab | undefined`
- `tab.kind`, `tab.index`, `tab.type`, `tab.name`, `tab.gold` (getters)
- `tab.items` -> `Unit[]`, built on each access, so listing tabs stays cheap
- `tab.click(x, y)` -> `boolean` (true = handed to the game)
- `tab.depositGold(amount)` / `tab.withdrawGold(amount)` -> `boolean`
- `clickItem(0, item)` works for items on tabs that are not shown
- `StashTabKind.personal` / `StashTabKind.shared`, `StashTabType.normal` /
  `advancedStash` / `chronicle` constants

## Gold

Gold is attributed per tab so one shape fits every backend, D2R included where
each shared tab really does hold its own gold. Where a game keeps one figure per
stash rather than per tab, it is attributed to the first tab of its kind and
every other tab reports 0. On 1.14d that is personal tab 0 carrying the
character's stash gold, the `STAT_GOLDBANK` stat.

`DepositGold` / `WithdrawGold` are fire and forget, like `gold()`: the request
is issued and the call returns; `Gold()` and the gold stats update when the
server's reply lands, so scripts poll for the change afterwards. On 1.14d they
run the vanilla gold dialog action (`GoldAction` with `Deposit` / `Withdraw`),
which needs the stash panel open. A backend whose pool only supports moving
"as much as fits" may ignore `amount`.

`GoldActionMode` carries the dialog's codes (`Drop = 1`, `Trade = 2`,
`Deposit = 3`, `Withdraw = 4`, per the reference command reference and kolbot's
`gold(amount, 3 | 4)` usage); `gold()` defaults to `Drop`, as the reference did.

## Character state

The character-state producer (`components/characterstate`) sends the stash as
`containers.stash.pages`, one entry per tab from `GetStashTabs()`, each carrying
the tab's `kind`, `index`, `type`, `name` and `gold` plus the shared grid
`width` / `height` and its `items` (from `StashTab::GetItems()`, so tabs that
are not shown are included). 1.14d produces a single personal page; `name` is
the tab's name, empty when unnamed. The stash hash folds every page's identity
and contents, so a rename, a gold move or a page appearing re-sends the
container.

## Generated docs

`StashTabKind` and `StashTabType` are both a `Types.h` enum and a JS constants
namespace of the same name. `gen_dts.py` emits both declarations for such a
name (`declare namespace X { ... }` for the values and `type X = 0 | 1` for the
type; TypeScript keeps them in separate declaration spaces), so doc `{type}`s
may name them inside composite types, e.g. `{kind:StashTabKind}`.
