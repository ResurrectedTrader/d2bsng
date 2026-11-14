# Belt pickup during a drink (1.14d)

A drink from a belt column and a pickup of a potion that goes to the same column, handled in the same server frame, leave the 1.14d client with the picked-up potion in no inventory. The next drink that moves it down halts the client in `CITEMS_ItemAction_UpdateStats` (`0x4C4C70`, `FOG_DisplayHalt` line 3166). A human client hits it too, but a bot that drinks while its pickit runs hits it far more often.

## The server's order

The belt use (C->S `0x26`) drinks at once: `ITEMMODE_UseBeltItem` (`0x562390`) sends the drunk potion's 0x9C remove-from-belt and calls `ITEMMODE_CompactBeltColumn` (`0x55EDC0`). That moves the potions above it down server-side, but only queues their updates (`INVENTORY_AddItemToCommandQueue`). The queue goes out as 0x9D update-stats packets when the player's update ends (`D2GAME_INVMODE_Last`, `0x597B00`).

The pickup (C->S `0x16`, `PLRMSG_InteractWithUnit` at `0x548B00`) happens in the packet handler when the item is within reach (distance 4, no collision). Otherwise the player walks there, and it happens in the player's update on arrival. It puts the potion in the column's first free slot, and its 0x9C put-in-belt (`0x53EE70`) goes out at once. If the pickup comes after the drink in the same frame, that first free slot is the one the compaction has just freed at the top of the column, and the put-in-belt goes out before the queued 0x9Ds. This happens when the 0x16 arrives after the 0x26, or when a walk started earlier ends in the drink's frame, whatever order the client sent them in. A pickup handled before the drink takes a slot that is free on the client as well. The compaction's moves then go out in row order (as the trace below shows), each into a slot the previous one freed, so that order is harmless.

A recorded case: column 0 holds potions A, B and C in slots 0, 4 and 8. The client sends a pickup of potion D and then a drink of A, in the same turn. The server answers in one frame:

| # | Packet | Meaning |
|---|---|---|
| 1 | 0x9C 0x0F, A, slot 0 | A leaves the belt |
| 2 | 0x0A, D | D leaves the ground |
| 3 | 0x9C 0x0E, D, slot 8 | D put in slot 8 |
| 4 | 0x9D 0x15, B, slot 0 | B moved down (queued) |
| 5 | 0x9D 0x15, C, slot 4 | C moved down (queued) |

## The client

At packet 3 the client still has C in slot 8. `CITEMS_ItemAction_PutInBelt` (`0x4C4130`) calls `INVENTORY_PlaceItemInSocket`, which links nothing for a player-owned inventory. It then calls `INVENTORY_PlaceItemInBeltSlot`, which fails on the occupied slot, and ignores the result. D has no parent inventory. Packets 4 and 5 move B and C correctly. When a later drink moves D down, its 0x9D finds the unit, but `INVENTORY_RemoveItemFromInventory` returns null and the `pRemoved == pBeltItem` assert halts (`0x4C4FDE`).

## The workaround

The receive intercept (P3, `hooks/Intercepts.cpp`, `CompactBeltColumnBeforePutInBelt`) looks at each 0x9C put-in-belt before the game handles it. It reads the target slot with `ITEMS_GetCompactItemDataFromBitstream`, as the handler does. If another item holds that slot, it compacts that column of the client's belt the way `ITEMMODE_CompactBeltColumn` does: each potion moves down to the lowest free slot, through `INVENTORY_PlaceItemInBeltSlot`, so it stays linked. The client's column then matches the server's. The slot is free for the put-in-belt, and each queued 0x9D recreates its potion where it already is.

It changes nothing when the slot is free, so belt traffic without the race is untouched. No packet is delayed, dropped or reordered, and outgoing packets are not held. It covers every timing that produces the order above, including a pickup the server completes after walking the player to the item. A put-in-belt into a slot that compaction cannot free (a genuine desync) still fails as natively. Each time it acts it logs `belt: compacted column ...` at debug level on the `hooks.intercepts` logger.
