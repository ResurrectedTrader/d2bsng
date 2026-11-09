# Fog memory-pool index locking (1.14d)

Two game-only instruction corrections use the existing `WriteByte` / `WriteN` facilities:

- `0x4095F5`: JL becomes JMP, bypassing the unlocked region-index reallocation. The original insertion helper at `0x409370` already grows the index under its system lock.
- `0x4099D9`: move the index-pointer load after EnterCriticalSection within the same 25-byte range. The original lookup algorithm stays intact. Removal's caller already holds the system lock.

Both original and already-corrected instructions are accepted, making reload idempotent. Both sites are validated before either write. These bytes contain no DLL addresses and intentionally remain after unload: restoring the races in a running allocator is unsafe. There are no allocator replacements, detours, ABI adapters or longer lock scopes.

Like the existing inline patches, installation must run before worker activity at suspended-process startup. Do not hot-inject this rewrite into an active allocator: moving instructions changes interior return addresses, and one already-blocked old lookup can retain its stale pointer. This PR does not add a separate thread suspension/patching framework.

Validation: original bytes, system-lock offset, insertion growth and removal caller inspected in IDA; Release backend build. Live simultaneous growth across different size classes and free/realloc stress remain required.
