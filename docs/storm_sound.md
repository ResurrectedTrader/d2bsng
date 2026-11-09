# Storm streamed-sound synchronization (1.14d)

The read worker drops Storm's critical section before I/O. Its sound-buffer check at `0x41A725` and stop branch run without it. Meanwhile `SFileDdaEnd` at `0x41A990` clears the active request's buffer and frees its stream under that lock. The refill at `0x4157C0` takes the lock, but assumes the earlier check is still valid.

A typed Detours slot redirects the sound-update basic block to a C++ replacement that holds the existing recursive Storm read lock around the check, refill and stop. The native refill already takes the same lock and calls DirectSound while holding it. Zero bytes plus a nonzero requested size retains the original stop condition.

A basic-block detour is necessary because hooking only the refill leaves the stop path unprotected. A small ESI adapter and a tail jump to `0x41A736` retain the worker's physical context; the trampoline is deliberately unused. The complete 17-byte original block is verified before attachment, although Detours itself owns the shorter jump and its restoration. There is no manual 17-byte CALL patch.

HookManager owns the slot. Install/remove during quiescent startup/shutdown; a worker already past the intercepted block at late injection can complete one old update. Live validation must repeatedly start/stop music and streams, including EOF/zero-space reads, with the worker delayed around its buffer check.

Related upstream: blizzhackers/d2bs `e658d9bba8fdc99869c2b6fa80cf489d13528780`.
