# PlugY multi-page stash (1.14d)

PlugY replaces the single LoD stash with any number of personal pages plus an
account-wide shared stash. The 1.14d backend surfaces those pages through the
game-agnostic stash-tab contract described in `docs/stash_tabs.md` (the
`game::StashTab` handle, the `StashTab` JS class, the per-tab gold rule and the
character-state feed); this documents only the PlugY side: how PlugY pages the
stash, how the backend detects a supported PlugY, initialises it when it was
injected without PlugY.exe, reads the page mirror, and drives a page switch so a
click can reach a page that is not shown.

Everything PlugY-specific lives in `backends/lod114d/game/PlugY.h` / `PlugY.cpp`
and `backends/lod114d/imports/extras/PlugY.h`. `game/Stash.cpp` delegates every
`StashTab` member to `plugy::` while `plugy::IsActive() && plugy::HasStashTabs()`
holds and otherwise
serves the vanilla single tab, so the contract, the JS bindings and the vanilla
path are untouched by PlugY's presence.

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
item; `plugy::IsParkedItem` is that test. `Unit::ItemLocation()` reports such an
item as `Stash`, `ClickItem` hands it to the page swap, and `Unit::StashTab()`
decides by page membership.

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
patches first. This was checked once against PlugY 14.03 by collecting every
1.14d entry of PlugY's `R8(...)` and `V114d ? offset + 0x...` patch sites with
the bytes each writes, and comparing them with every d2bs site (the
`Intercepts.cpp` RVA table with install lengths, plus the Detours targets
`CursorLock`, `SSTR_RegistryReadValueEx` and `RegStoringKeysConfiguration` with
a 16-byte prologue allowance): 101 PlugY sites, zero intersections. The closest
pair is in the same function: d2bs's multi-instance bypass replaces the
`FindWindowA` call at 0xF5623 (6 bytes) and PlugY's `ActiveLaunchAnyNumberOfLOD`
flips the `je` two bytes later at 0xF562B to `jmp`. They do not touch the same
bytes and want the same outcome (the bypass returns no window, so the jump is
taken either way). Redo the comparison when either side gains a hook.

## Detection

`backends/lod114d/game/PlugY.cpp`, on first use, in this order. A final verdict is
cached and logged once; any failure keeps the feature off (vanilla behaviour):

1. **Module.** `GetModuleHandleW(L"PlugY.dll")`. A miss is retried until a
   player unit exists: the DLL is loaded at Game.exe's startup (by PlugY.exe or
   by the manager ahead of d2bs), so with a player in the game a missing module
   is final and a vanilla install pays one atomic load per query from then on.
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
   A miss here is likewise not cached straight away: on the
   manager-injection path PlugY's `Init` runs at Game.exe's startup, possibly
   after analytics first asks, so it becomes final (and is logged as
   `ActiveMultiPageStash=0`) only once a player unit exists.
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

Every `plugy::` read requires `IsActive()`; every caller, `Stash.cpp` and
the generic item paths in `Unit::ItemLocation` / `ClickItem` alike, pairs it
with `HasStashTabs()` (the mirror's `currentStash` is set) before calling
anything else here. The page walks, the item walks and the switch planner are
member functions of the `PYPlayerData` / `Stash` mirrors in
`imports/extras/PlugY.h`, which also holds the 0x3A command bytes (no data
added, sizes still asserted). All reads run under `GameReadLock`, since PlugY
relinks the lists on the game thread during a page switch. Page names are ANSI (typed into PlugY's in-game text box)
and converted to UTF-8. Walks are capped at 65536 pages as a guard against a
corrupted list.

## Gold

PlugY pages have no gold of their own, so the stash-wide figures land on the
first tab of each kind. Personal page 0 reports the character's stash gold, the
`STAT_GOLDBANK` stat, and its `DepositGold` / `WithdrawGold` are the vanilla gold
dialog exactly as without PlugY. Shared page 0 reports PlugY's single shared
pool, `PYPlayerData::sharedGold`, enabled by `ActiveSharedGold=1` and persisted
in the shared stash file. Every other page reports 0 and refuses gold moves.

The pool is moved with the stash panel's put/take buttons, which send 0x3A
commands 0x26 (put) and 0x27 (take); the server side hard-codes "as much as
fits" (all carried gold into the pool, or as much of the pool as the character
can carry), so `amount` is ignored for the shared tab. The server answers with
its shared-gold update (0x9D, function 0x19), which sets the mirror's
`sharedGold`. `plugy::MoveSharedGold` is fire and forget like the panel button:
it sends the command and returns, and the tab's `gold` catches up when the
reply lands. There is no ordering hazard to wait for (unlike the item click
before a page revert), and waiting for "the value changed" would burn the whole
timeout whenever the server has nothing to move yet still echoes the unchanged
pool. No page switch is involved: the pool is independent of the page shown.

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
   live: the item showed on the cursor but could not be placed). Every stash
   click that does anything (pick up, drop, swap) changes what is on the cursor,
   so the cursor item id before and after the click is both the "did it send
   anything" test and the id set to wait for. The wait is for a server item
   action packet (0x9C, or 0x9D with a vanilla action byte below PlugY's 0x18
   function range) naming the item that left or reached the cursor. A
   backend-internal tap on the incoming packet intercept
   (`hooks::intercepts::SetIncomingPacketObserver`) records the item ids it sees
   on the game thread; the script thread polls with locks released. A click that
   changed nothing (empty cell with an empty cursor, a placement the game
   rejected) sent nothing and skips the wait. Timeout 2 s, logged; the click
   result is still returned.
7. Send the reverse plan and wait the same way as step 4. A revert that times
   out is logged; the click result is still returned since the item did move.

`ClickItem` hands off to `plugy::ClickParkedItem` for a parked item
(`IsParkedItem`). `WithActivePage` refuses to re-enter on the thread already
running an operation, so a hand-off from inside the swapped-in click cannot
deadlock on the operation mutex.

Cost: roughly two frames per direction in single player or TCP/IP, plus the
click. Manual clicks on PlugY's own buttons during an operation are not guarded
against; the timeout is the backstop.

## Removing PlugY support

Delete `backends/lod114d/game/PlugY.h`, `PlugY.cpp` and
`imports/extras/PlugY.h`, drop the `plugy::` calls in `Stash.cpp`, the
`IsParkedItem` hand-offs in `ClickItem` (`GameHelpers.cpp`) and
`Unit::ItemLocation` (`Unit.cpp`), the feature tag in `GameHelpers.cpp`, and the
`InstallInitHook` call in `Bridge::Init`. The contract, `Stash.cpp`'s vanilla
path and the JS bindings stay; they are what a D2R backend implements.

## Not done (yet)

- PlugY's page rename / insert / delete are not exposed.
