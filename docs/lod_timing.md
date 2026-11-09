# Single-player server clock rollover (1.14d)

The server step at `0x52FC20` masks timeGetTime to 31 bits and checks elapsed time with a signed comparison. Crossing a 2^31-millisecond boundary while a single-player game is running makes now smaller than the last update and prevents simulation frames from advancing. Clock speed scaling can make the boundary arrive sooner.

Two checked byte changes preserve the native function: the AND immediate becomes all ones (`0x52FC4B`: 7F -> FF), and JGE becomes JAE (`0x52FC64`: 7D -> 73). Unsigned DWORD subtraction then handles the 2^32 rollover too, provided updates remain less than one full DWORD cycle apart. Both instruction shapes are checked, accepting already-fixed values. Existing `WriteByte` installs them at startup.

The bytes contain no DLL references and intentionally stay after unload: restoring a masked clock after storing a full-width last timestamp can freeze the game. Install at suspended-process startup; arbitrary late injection and switching clocks mid-frame are not covered.

The script API already uses `std::chrono::steady_clock`, converts milliseconds to a JavaScript number without 32-bit narrowing, and shares that clock with `me.gamestarttime`. It needs no GetTickCount64 migration and no script/kolbot changes.

Validation: instruction shapes and arithmetic checked in IDA; Release backend build. Live clock-boundary testing remains required, including both catch-up modes and speed scaling.
