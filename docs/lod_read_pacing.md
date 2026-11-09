# Storm resource-read pacing bypass (1.14d)

At startup, a checked branch at `0x41A588` changes from JZ to JMP through the existing `WriteByte` facility. The worker keeps its rate interval at zero, matching Storm's own unpaced mode. Queued resource reads can drain promptly; empty queues still wait on the normal event with the original timeout.

This is an unconditional performance change, not a crash repair or a shutdown-only patch. It applies throughout gameplay and can increase disk/CPU activity. Hook removal restores the original byte. No game function is replaced and no scripts need changes.

Validation: original branch and worker queue/wait logic inspected in IDA; Release backend build. Live exit-latency and resource-usage comparisons remain required, including empty-queue idle, crowded-area traversal and sound playback.
