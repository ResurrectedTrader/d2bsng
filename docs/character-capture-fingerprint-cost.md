# Character-capture change detection

`CharacterState::OnTick` runs on the game thread under the frame write lock, about once a
second per instance. Every tick it fingerprints the character's containers and wearer to
decide whether anything moved and needs sending to the manager. The fingerprint runs whether
or not anything changed, so it is the recurring cost, not the send.

## The problem it fixed

The fingerprint used to be computed by building each item's JSON document, dumping it to a
string, and hashing the string - the same `UnitToJson` used to build the wire payload, run
purely to detect change. In-game measurement (a temporary per-phase timing log, since
removed) put the container fingerprint at **~1.3-1.7 ms per tick, with spikes to ~4 ms**,
while the identical field walk fed straight into a hash took **~0.02 ms**. So ~98% of it was
JSON tree allocation + string dump, not the game-memory field reads - and the spikes were
allocator jitter, absent from the allocation-free hash. The synchronous `WM_COPYDATA` send,
once suspected, measured ~0.09 ms: cheap, and not the recurring drain.

The accessors being negligible surprised us - the hypothesis had been that reading game
memory would dominate. An injected DLL reads the structs directly rather than over IPC, so a
field read is a pointer dereference.

## The design

One traversal, two sinks. `VisitUnit` (in `UnitJson.cpp`) walks a unit's fields once and
emits them to a `UnitVisitor`:

- `JsonVisitor` builds the `nlohmann::json` wire document - `UnitToJson`, used to build the
  payload for the sections that actually changed.
- `HashVisitor` (in `Fingerprint.cpp`) folds the same fields into an FNV-1a hash with no
  allocation - `ContainerHash` and `UnitHash`, used every tick to detect change.

Because both go through the one field list, a field added to the traversal is picked up by
both automatically. The fingerprint cannot silently stop covering a field the payload still
sends, which a separate hand-written hash walk could. `Detail::Structural` gates the
Full-only presentation fields (title, description, statsLists) out of the traversal, so the
fingerprint never sees them - changing a tooltip does not force a resend.

The wire document is unchanged: the JSON sink produces the same keys, values and nesting as
before (object key order is `std::map`-sorted on dump, so visit order does not affect it).

## Result

The per-tick fingerprint drops from ~1.3-1.7 ms to ~0.02 ms for the containers, and the
per-tick `UnitToJson` build of the wearer documents is gone - they are built only when their
fingerprint says they changed. The multi-millisecond spikes go with the allocation. See
`CharacterCaptureTest.cpp` for the wire-format and fingerprint-coverage guards.
