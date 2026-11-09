# LvlSub table bounds (1.14d)

The placement and theme-selection walkers at `0x670750` and `0x6706A0` use a change of type as their only terminator. The last type has no following row to inspect. Small checked loop-tail patches at `0x670783` and `0x670724` compare the next row against `table + count * 0x15C` before loading it.

Both original functions remain intact: row order, probability rolls, signed comparisons, DT1 masks and placement all execute their original instructions. The stubs use the existing InlinePatch/InstallSite/RestoreSite lifecycle and verify the complete old instruction sequences. Only EAX/ECX are scratch; EDX retains the picker's subtype. Continuations are original instruction boundaries. Install/remove at quiescent lifecycle points, as with the existing inline patches.

Validation: original bytes, table globals, register liveness and continuations checked in IDA; Release backend build. Live repeated Act 5 generation stress is still required.

Related upstream: blizzhackers/d2bs `501f3518220026810782edca2adc4e86a3802504`.
