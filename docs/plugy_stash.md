# Stash tabs (PlugY multi-page stash)

Scripts can enumerate the tabs of the player's stash, read the items on any
tab, and click into any tab, including tabs that are not currently shown. The
contract is game-agnostic (vanilla LoD, PlugY, a future D2R backend); this
documents the contract, how PlugY implements pages on 1.14d, and how the
backend reads and drives them.

## Contract

`src/contract/game/Types.h`:

```cpp
enum class StashTabKind : uint8_t { Personal = 0, Shared = 1 };
enum class StashTabType : uint8_t { Normal = 0, AdvancedStash = 1, Chronicle = 2 };

struct StashTab {
    StashTabKind kind;   // personal or account-wide shared
    uint32_t index;      // 0-based position within its kind
    StashTabType type;   // always Normal on LoD; D2R has stackable and Chronicle tabs
    std::string name;    // user-given name, empty when unnamed
    bool isActive;       // the tab whose items are in the player's inventory right now
    uint32_t gold;       // gold stored on the tab (see Gold)
};
```

`src/contract/game/GameHelpers.h` / `Unit.h`:

| Function | Meaning |
|----------|---------|
| `GetStashTabs()` | personal tabs first, then shared; empty with no player unit |
| `GetStashTabItems(kind, index)` | items on one tab, active or not; empty for an unknown tab |
| `Unit::StashTab()` | the tab holding an item; nullopt unless it is in the player's stash |
| `ClickStashTabSlot(kind, index, cell)` | left-click a cell of any tab (pick up / drop / swap); blocking |
| `ClickItem(button, item)` | unchanged signature, now also reaches items on inactive tabs |
| `StashTabGold(kind, index, mode, amount)` | deposit into / withdraw from a tab's gold, `gold()`'s mode codes |

There is deliberately no "select tab" in the contract. A tab switch is a server
round trip on PlugY but a pure UI action on D2R, so exposing it would force
scripts to sequence something that only one backend needs. Instead the click
calls own the switch: on PlugY they swap the tab in, click, and swap back before
returning, and D2R's tab-addressed packets need no switch at all. Scripts see
one synchronous call either way.

Only a left click is offered for tab cells. Neither LoD nor PlugY has a modifier
click that moves an item to the stash (that is a D2R addition): shift-click is
the game's quick action (belt / tome / sell) and right-click uses a consumable,
so on a stash cell only the left click does anything. `ClickItem` keeps its
`button` for parity.

Placement rules are the game's: `LeftClickItem` drops on an empty footprint,
swaps when exactly one item is under it, and rejects the click otherwise, and
the server validates the resulting 0x18 drop packet. `Dispatched` therefore
means "handed to the game", as for every other click; scripts confirm with
`GetStashTabItems` / `Unit::StashTab` afterwards.

`isActive` exists because the pre-existing item API (`GetItems`, `FindInvItem`,
JS `getItems()` / `getItem()`) only ever sees the inventory, and on PlugY that
means the active page only. The flag tells a script which tab that view is
showing. That view is left alone on purpose: legacy scripts assume "stash items
= location 7 in the inventory", and the extra tabs are reachable only through
the new calls.

### Vanilla LoD

One personal tab, index 0, always active. `GetStashTabItems(Personal, 0)` is the
inventory walk filtered to `ItemLocation::Stash`. Implemented in
`backends/lod114d/game/Stash.cpp`, which also owns the fallback whenever PlugY
is absent, disabled, or (on Battle.net) inactive.

### JS surface

- `getStashTabs()` -> `Array<{kind, index, type, name, isActive, gold}>`
- `getStashTabItems(kind, index)` -> `Unit[]`
- `unit.stashTab` -> `{kind, index, type, name, isActive, gold} | undefined`
- `clickStashTab(kind, index, x, y)` -> `boolean` (true = handed to the game)
- `clickItem(0, item)` works for items on inactive tabs
- `stashTabGold(kind, index, amount, mode)` -> `boolean`, mode 3 deposits, 4
  withdraws, exactly as `gold(amount, mode)`
- `StashTabKind.personal` / `StashTabKind.shared`, `StashTabType.normal` /
  `advancedStash` / `chronicle` constants

### Gold

Gold is attributed per tab so one shape fits every backend, D2R included where
each shared tab really does hold its own gold. Where a game keeps one figure per
stash rather than per tab, it is attributed to the first tab of its kind and
every other tab reports 0.

- **Vanilla LoD**: personal tab 0 carries the character's stash gold, the
  `STAT_GOLDBANK` stat. `StashTabGold` on it is the vanilla gold dialog
  (`GoldAction` with Deposit = 3 / Withdraw = 4, the codes kolbot passes to
  `gold()`); the stats update asynchronously as with `gold()`. Both paths are
  fire and forget: `StashTabGold` returns once the request is issued and
  scripts poll `GetStashTabs` / the gold stats for the result.
- **PlugY**: pages have no gold of their own. Personal page 0 reports the same
  `STAT_GOLDBANK` stat. Shared page 0 reports PlugY's single shared pool,
  `PYPlayerData::sharedGold`, enabled by `ActiveSharedGold=1` and persisted in
  the shared stash file. The pool is moved with the stash panel's put/take
  buttons, which send 0x3A commands 0x26 (put) and 0x27 (take); the server side
  hard-codes "as much as fits" (all carried gold into the pool, or as much of
  the pool as the character can carry), so `amount` is ignored there. The
  server answers with its shared-gold update (0x9D, function 0x19), which sets
  the mirror's `sharedGold`. `plugy::MoveSharedGold` is fire and forget like
  the panel button: it sends the command and returns, and the tab's `gold`
  catches up when the reply lands. There is no ordering hazard to wait for
  (unlike the item click before a page revert), and waiting for "the value
  changed" would burn the whole timeout whenever the server has nothing to
  move yet still echoes the unchanged pool. No page switch is involved: the
  pool is independent of the page shown.

`GoldActionMode` now carries the dialog's real codes (`Drop = 1`, `Trade = 2`,
`Deposit = 3`, `Withdraw = 4`, per the reference command reference and kolbot's
usage). It previously said `Drop = 0` / `Stash = 1` and `gold()` defaulted to
`Stash`; the numeric default stays 1, which is drop, exactly as the reference
defaulted, so no script sees a change.

## How PlugY implements pages (1.14d)

PlugY never teaches the game about pages. The player's inventory only ever holds
the items of the selected page (inventory page 4, `ItemLocation::Stash`). On a
page switch PlugY:

1. detaches every stash item from the inventory with the game's own
   `INVENTORY_RemoveItem` and threads them onto the outgoing page's list through
   the item's `pItemData->pExtraData.pNextItem`;
2. re-adds the incoming page's items with `INVENTORY_AddItem` at their stored
   grid coordinates.

The parked units stay alive in the client's unit hash table and keep their grid
position, so a `game::Unit` handle built with `Unit::FromPtr` resolves and reads
like any other item. Two fields do change: `pParentInv` is null, which is why
the vanilla inventory walk never reaches them, and the game's removal routine
zeroes the node byte (`nNodePos`, the one `ItemLocation()` reads), so the raw
value says Ground. PlugY saves and restores only the neighbouring `nNodePosOther`
byte. The item's mode stays `IMODE_STORED`, and no vanilla item is ever
stored-mode with no parent inventory, so that pair is the signature of a parked
item. `Unit::ItemLocation()` reports such an item as `Stash` once page
membership confirms it, `ClickItem` uses the same signature to hand off to the
page swap, and `Unit::StashTab()` decides by membership alone when PlugY pages
exist.

The bookkeeping is a 28-byte `PYPlayerData` PlugY appends to the game's
`D2PlayerDataStrc` allocation:

```
PYPlayerData { flags, sharedGold, nbSelfPages, nbSharedPages,
               Stash* currentStash, Stash* selfStash, Stash* sharedStash }
Stash        { id, flags(isShared, isIndex, isMainIndex), char* name,
               D2UnitStrc* ptListItem, Stash* previousStash, Stash* nextStash }
```

Two doubly linked lists (personal, shared); the active page's `ptListItem` is
always null. Structs are mirrored in `backends/lod114d/imports/extras/PlugY.h`
with `static_assert`s. The layout is byte-identical in every 1.14d-capable
release (12.00 through 14.03).

Both client and server keep a mirror (in 1.14d they share the process for
single-player and TCP/IP hosting). The server is authoritative and pushes page
changes to the client with a custom S->C packet 0x9D; the client requests them
by overloading the vanilla C->S 0x3A "spend stat point" packet with
out-of-range command bytes. The backend reads only the client mirror.

PlugY skips all of this on Battle.net (`onRealm`), and leaves the lists empty
until the character is loaded, so `currentStash == nullptr` means "no pages":
the backend then serves the vanilla single tab.

## Loading PlugY without PlugY.exe

PlugY.exe starts Game.exe as a debuggee, patches its first `LoadLibraryA` call
to also load PlugY.dll and call its exported `Init`, then exits. A manager that
tracks the launcher PID is left with an orphaned game. The alternative is for
the manager to inject PlugY.dll into the suspended Game.exe first, then d2bs.dll:
PlugY's `DllMain` does nothing, so d2bs runs `Init` for it.

`Init` cannot simply be called from `DllMain`: d2bs attaches before Game.exe's
entry point, and PlugY's `loadParameters` allocates through the game's own
`D2FogMemAlloc` and calls `D2FogGetSavePath`, so with Fog's memory pool not yet
initialised the call crashes inside PlugY. PlugY.exe avoids this by redirecting
Game.exe's startup `call [LoadLibraryA]` (the one that loads advapi32, RVA
0x621C, bytes `FF 15 44 C1 6C 00`): by the time it executes Fog is up, and none
of the code PlugY's startup-time features hook has run.

`plugy::InstallInitHook()`, called from `Bridge::Init`, does the same from
inside the process. If PlugY.dll is loaded and the site still holds the original
indirect call through the IAT slot that resolves to `LoadLibraryA`, it replaces
the 6-byte call with a call to a `__stdcall` replacement of the same shape
(`hooks::WriteCallN`, as the multi-instance bypass does for `FindWindowA`). The
replacement runs PlugY's `_Init@4` once, with the current directory set to the
game folder because `Init` reads `PlugY.ini` relative to it, then performs the
original `LoadLibraryA`. Without the module it is a no-op, and if the bytes are
not the original call (PlugY.exe already redirected it) it leaves the site to the
launcher's stub. d2bs never loads PlugY.dll itself.

Consequence for ordering: PlugY's patches now land after d2bs's hooks, which
`InstallAll` writes during `DllMain`. That is safe only because no PlugY site
overlaps a d2bs site (see below); PlugY verifies the bytes it expects at each
site and exits the process on a mismatch, so an overlap would surface as PlugY
refusing to start, not as silent corruption. If d2bs is injected after the
startup call has already executed, the hook never fires and PlugY stays
uninitialised; detection then logs "loaded without the multi-page stash".

`Release` is never called; on 1.14d it only closes PlugY's log. PlugY.exe's
`Param=` and `-w` command-line additions from `PlugY.ini` do not apply on this
path; the manager's command line is the only one.

### Patch-site overlap

PlugY writes raw bytes at fixed RVAs and verifies the bytes it expects to find
there first, so it must not share a site with d2bs's hooks, whichever side
patches first. `scripts/plugy_overlap_audit.py <PlugY source dir>` checks this: every
1.14d entry of PlugY's `R8(...)` and `V114d ? offset + 0x...` patch sites with
the bytes each writes, versus every d2bs site (the `Intercepts.cpp` RVA table
with install lengths, plus the Detours targets `CursorLock`,
`SSTR_RegistryReadValueEx` and `RegStoringKeysConfiguration` with a 16-byte
prologue allowance). Against PlugY 14.03: 101 sites, zero
intersections. The closest pair is in the same function: d2bs's multi-instance
bypass replaces the `FindWindowA` call at 0xF5623 (6 bytes) and PlugY's
`ActiveLaunchAnyNumberOfLOD` flips the `je` two bytes later at 0xF562B to `jmp`.
They do not touch the same bytes and want the same outcome (the bypass returns
no window, so the jump is taken either way). Re-run the audit when either side
gains a hook.

## Detection

`backends/lod114d/game/PlugY.cpp`, on first use, in this order. Any failure keeps
the feature off (vanilla behaviour) and logs once:

1. **Module.** `GetModuleHandleW(L"PlugY.dll")`. A miss is not cached: the DLL
   is normally in the process before d2bs is, but the lookup is cheap.
2. **Version allowlist.** The DLL ships a VERSIONINFO resource
   (`FILEVERSION 14,0,3,0`, shown as "14.03"). Only 12.00, 14.00, 14.01, 14.02
   and 14.03 are accepted. Those are the official releases that contain the
   1.14d port (it arrived with the haxifix merge, first shipped in 12.00; 11.02
   and earlier have no 1.14d column in their address tables at all). For each of
   the five, the PlugY git history was diffed for everything this file depends
   on, and all of it is identical: the `Stash` / `PYPlayerData` layouts, the
   allocation patch site and its expected original bytes, the size-immediate
   derivation, the `ActiveMultiPageStash` gating of that patch, the 0x3A command
   values, the 0x9D `DataPacket` layout, the server/client handler patch sites,
   and the page-list semantics (id equals position, `ptListItem` null on the
   active page, next/previous/first/kind-switch behaviour). What did change
   across them (index-flag bookkeeping, page renaming, load-time page selection)
   is never read here. A version number the resource cannot vouch for, such as
   a third-party build stamped with an older number, is rejected rather than
   guessed at.

   To admit a new release, re-check that list against its source and, if it
   still holds, add the version to `SUPPORTED_VERSIONS`; if the layout moved, it
   needs its own struct set keyed by version.
3. **Patch check.** The extension only exists when `ActiveMultiPageStash=1`:
   `Install_MultiPageStash` -> `Install_PlayerCustomData` redirects the
   allocation call inside `D2Common::InitPlayerData` (Game.exe RVA 0x221F90) to
   PlugY's own allocator. The backend decodes the `call rel32` at
   InitPlayerData+0x4C and requires its target to lie inside PlugY.dll's image.
   With the module loaded but the feature off, the player data is the vanilla
   block and the bytes past it belong to whatever the Fog pool placed next, so
   this check is what makes the read safe. Reading PlugY.ini would answer the
   same question less reliably (configurable file name, D2mod.dll indirection,
   default/fixed overlays).
4. **Size check.** PlugY places its tail at `pPlayerData + <imm32 of the
   mov edx at InitPlayerData+0x47>`; the backend reads the same immediate and
   requires it to equal `sizeof(D2PlayerDataStrc)` (0x16C), so both sides agree
   on where the extension starts.

Bytes verified against a 1.14d Game.exe:

```
0x621FD7  BA 6C 01 00 00   mov edx, 0x16C
0x621FDC  E8 4F 94 DE FF   call 0x40B430   (Fog alloc; PlugY rewrites the rel32)
```

A detected PlugY adds the `plugy` tag to `game::GetActiveFeatures()` (analytics
feature list). Never the version number.

## Reads

All reads run under `GameReadLock`, since PlugY relinks the lists on the game
thread during a page switch. Page names are ANSI (typed into PlugY's in-game
text box) and converted to UTF-8. Walks are capped at 65536 pages as a guard
against a corrupted list.

## Clicking into an inactive page

`plugy::WithActivePage(kind, index, action)` runs on the calling script thread:

1. Release the thread's read locks (`GameReadLockReleaser`) and take a
   process-wide operation mutex, so two scripts cannot interleave switches. The
   release comes first: a thread queued on the mutex while holding a read lock
   would stall the game thread the running operation is waiting on.
2. Read the active page from the mirror, remember it.
3. Plan the switch from the mirror and send every 0x3A command at once. PlugY
   only has relative moves: a kind change goes through to-personal (0x1B) /
   to-shared (0x1C), which land on that kind's first page; within a kind it is
   previous (0x19) / next (0x1A) singles, or first-page (0x1F) when that is
   shorter. The target must already exist in the mirror because "next" past the
   last page creates a page server-side, and a script bug must not mint pages.
   The client's list can only lag the server's, never lead it, so the clamp is
   safe.
4. Wait for the mirror to show the target active: poll under a brief read lock,
   sleep 5 ms between polls with locks released, give up after 3 s
   (`StashTabUnavailable`).
5. Run the action, which is the ordinary `ClickContainerSlot` / `ClickItem`; by
   now the page's items are back in the inventory with their grid positions.
6. Wait for the server to acknowledge the click. The client applies an item
   click locally first and tells the server afterwards, so a page switch sent
   straight after the click overtakes it: the server parks the page, then sees a
   pick-up for an item that is no longer in the stash, rejects it, and the client
   is left holding an item the server still has in the page (this was observed
   live: the item showed on the cursor but could not be placed). The wait is
   for a server item action packet (0x9C, or 0x9D with a vanilla action byte
   below PlugY's 0x18 function range) whose item id is one the click can touch:
   the cursor item or any item on the target page, collected before the click.
   A backend-internal tap on the incoming packet intercept
   (`hooks::intercepts::SetIncomingPacketObserver`) sees the packet on the game
   thread; the script thread polls a flag with locks released. A click that can
   move nothing (empty cursor, empty page) skips the wait. Timeout 2 s, logged;
   the click result is still returned.
7. Send the reverse plan and wait the same way as step 4. A revert that times
   out is logged; the click result is still returned since the item did move.

`ClickItem` hands off to `plugy::ClickParkedItem` for a stored-mode item with
no parent inventory, which is exactly the parked case. `ClickParkedItem` refuses
an item whose page is already active so the hand-off cannot loop.

Cost: roughly two frames per direction in single player or TCP/IP, plus the
click. Manual clicks on PlugY's own buttons during an operation are not guarded
against; the timeout is the backstop.

## Removing PlugY support

Delete `backends/lod114d/game/PlugY.h`, `PlugY.cpp` and
`imports/extras/PlugY.h`, drop the `plugy::` calls in `Stash.cpp`, the
`ClickParkedItem` hand-off in `ClickItem` and the feature tag in
`GameHelpers.cpp`, the parked-item rule in `Unit::ItemLocation`, and the
`InstallInitHook` call in `Bridge::Init`. The contract, `Stash.cpp`'s vanilla path and the JS bindings
stay; they are what a D2R backend implements.

## Character state

The character-state producer (`components/characterstate`) sends the stash as
`containers.stash.pages`, one entry per tab from `GetStashTabs()`, each carrying
the tab's `kind`, `index`, `type`, `name` and `gold` plus the shared grid
`width` / `height` and its `items` (from `GetStashTabItems`, so inactive PlugY
pages are included). Vanilla produces the single personal page it always did,
except that `name` is now the tab's real name (empty when unnamed) rather than
the literal "Personal". The stash hash folds every page's identity and
contents, so a rename, a gold move or a page appearing re-sends the container;
`isActive` is deliberately not sent, since scripts clicking into other tabs
would flip it constantly for no benefit to the manager.

## Not done (yet)

- PlugY's page rename / insert / delete are not exposed.
