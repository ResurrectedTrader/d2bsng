# Optional Storm read pacing bypass (1.14d)

`-unpacedreads` changes one checked branch at `0x41A588` from JZ to JMP using the existing `WriteByte` facility. The worker keeps its rate interval at zero, matching Storm's own unpaced mode. Queued resource reads can drain promptly; empty queues still wait on the normal event with the original timeout.

This is an opt-in performance change, not a crash repair or a shutdown-only patch. It applies throughout gameplay and can increase disk/CPU activity. Without the switch the game is unchanged; removing hooks restores the original byte. No game function is replaced and no scripts need changes.

Validation: original branch and worker queue/wait logic inspected in IDA; Release backend build. Live exit-latency and resource-usage comparisons remain required, including empty-queue idle, crowded-area traversal and sound playback.
