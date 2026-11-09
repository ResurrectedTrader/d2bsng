# Sprite-cache synchronization draft (1.14d)

The read worker and game thread share the DCC sprite cache. Native eviction selects/unlinks outside the cache lock; the decode callback modifies pin flags without locking and invokes an eviction-capable GrowBlock. Native unlink also refuses to move a pinned list head, although touch appends that entry regardless.

This draft locks victim selection/unlink and final accounting, releases resources outside the lock (release can wait for the reader), and clears the decode pin together with its read charge. Completed reads never evict. Issuing trims exclude their entry by identity and retain the most recently touched sprite, which can still be in use by the current draw. Native allocation and its 15-second policy are retained; there is no new pre-allocation trim hook.

Typed Detours replace eviction and grow. Only the pin block needs a small ESI/EDI-context adapter. Three checked NOP ranges use existing WriteN/RestoreN facilities: remove the unlocked pin clear and the two pinned-head unlink refusals. The original touch and allocation functions remain intact.

All sites must install/remove at quiescent lifecycle points. The producer side assumes d2bsng's existing game-thread dispatch: this is not a general-purpose concurrent sprite-loader design. This draft needs live stress testing before merge: repeated crowded-area traversal, queued-read cancellation, cache-pressure drawing, pinned-head touch, and delayed read completion during eviction. Compare cache accounting and graphics against baseline. This is the broadest change of the game-fix series and is intentionally separate.
