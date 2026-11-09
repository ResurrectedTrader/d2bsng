# Storm streamed-sound synchronization (1.14d)

The read worker drops Storm's critical section before I/O. Its sound-buffer check at `0x41A725` and stop branch run without it. Meanwhile `SFileDdaEnd` at `0x41A990` clears the active request's buffer and frees its stream under that lock. The refill at `0x4157C0` takes the lock, but assumes the earlier check is still valid.

A checked 17-byte call-site patch uses the existing `WriteCallN` / `RestoreN` lifecycle. Its small helper holds the existing recursive Storm read lock across the check, native refill and stop. The native refill already takes this lock and calls DirectSound while holding it. The original zero-byte/nonzero-requested-size stop condition is retained.

Only the worker's ESI argument and the refill's ESI ABI need assembly adapters. The thunk skips the CALL's 12 trailing NOP bytes and resumes at the original `0x41A736` instruction boundary. A branch-only edit cannot add the required critical-section scope; no complete worker function is reimplemented.

The full original sequence is verified before writing. HookManager owns installation/restoration. Install/remove at quiescent lifecycle points, like existing inline patches. Live validation must repeatedly start/stop streams, including EOF/zero-space reads and termination around the buffer check.
