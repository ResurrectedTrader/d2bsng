# Fog memory-pool index locking (1.14d)

`FOG_MemAllocateBlock` at `0x4095E0` grows the shared region index before acquiring its system lock. `FOG_MemFindBlock` at `0x4099D0` captures the array pointer before locking. A concurrent resize can lose an insertion or leave lookup searching freed memory.

Entry detours acquire the existing recursive system critical section before calling either original trampoline. This preserves the original allocator, growth policy, OOM paths and lookup algorithm. Allocation holds the system lock for its complete block allocation (including VirtualAlloc), so this trades a longer critical section for avoiding permanent byte rewrites. The original insertion and lookup recursively take the same lock. Region removal's caller already holds it and needs no extra hook. This does not claim to repair unrelated pool lock-order or lifetime issues.

Only the custom EAX/RET4 entry and trampoline calls use ABI adapters; the locking uses the existing RAII guard. HookManager owns attachment and removal. Install before game worker activity; Detours cannot retroactively protect a call already executing the original body during late injection. Detachment likewise requires a quiescent lifecycle.

Validation: inspect original entry ABIs, callers, system lock offset +4 and recursive inner acquisitions in IDA; build Release; stress concurrent allocation across different pools and free/realloc during repeated index growth in a live game. Live stress validation remains required.

Related upstream: blizzhackers/d2bs `7cfc19cbb7035045569d5bc59229d5789865b409`.
