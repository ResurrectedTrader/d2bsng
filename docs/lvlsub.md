# LvlSub table bounds (1.14d)

The placement and theme-selection walkers at `0x670750` and `0x6706A0` used a change of type as their only terminator. The final type has no following row to inspect. Both entry points are detoured through the typed Detours slots; replacements check the loaded table's end before reading a row. Valid rows retain their order, probability rolls and DT1 masks.

The placement helper at `0x66F990` has an EBX argument, so a small ABI adapter remains. There are no new mid-function patches. The hooks attach and detach with HookManager's other backend detours. These repairs affect the mapped 1.14d game, including levels initialized for script room queries.

Validation: compare all valid row sequences and RNG state against the original, then repeatedly generate the Act 5 outdoor levels with the last table group at the allocation boundary. Runtime stress validation is still required.

Related upstream fix: blizzhackers/d2bs `501f3518220026810782edca2adc4e86a3802504`.
