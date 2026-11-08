# Script code cache

Compiling a script's sources is a cost this framework pays far more often than a normal V8 embedder does. Every script
runs in its **own isolate**, so V8's per-isolate compilation cache never spans two scripts: a bot that starts half a
dozen script threads per game re-parses and re-compiles the same libraries once per isolate, every game. With kolbot
that is several hundred KB to a couple of MB of JavaScript per isolate, and `CompileSource` compiles eagerly, so
nothing is deferred.

`runtime::script::CodeCache` (`components/script/CodeCache.h`) keeps what repeats across isolates: V8's code-cache
blobs (the serialized result of parsing and compiling a source), which are isolate-independent.

## Tiers

| Tier | Always on | Covers |
|------|-----------|--------|
| Memory | yes | every isolate after the first, for the life of the process |
| Disk | opt-in (`CodeCachePath`) | the first compile after launch, plus other game instances pointed at the same directory |

Both are byte-capped. Memory evicts least-recently-*used*; disk evicts least-recently-*written*, because a read never
touches the file and touching it on every hit would trade away the I/O this cache exists to avoid. The disk tier is
otherwise a straight extension of the memory tier: a disk hit is promoted into memory, and a store writes both.

The cache holds byte vectors and never a V8 handle, so its lifetime carries no ordering dependency on the engine's
teardown.

## Where it hooks in

Entirely inside `CompileSource` - the one funnel every user-authored source goes through (`Script::RunScript`,
`Script::Include`, the console REPL, the compatibility prelude, and the `Sandbox` compile / include).

- **Miss** - compile with `kEagerCompile`, then `ScriptCompiler::CreateCodeCache` and store. Eager is what makes the
  stored blob complete; a lazily compiled script serializes only the functions that happened to run.
- **Hit** - compile with `kConsumeCodeCache` (mutually exclusive with `kEagerCompile`), which deserializes instead of
  parsing.
- **Rejected hit** - V8 validates the blob itself and, on mismatch, silently falls back to compiling the source
  *lazily* and sets `rejected`. Re-serializing at that point would persist a partial blob, so the entry is dropped
  instead and the next compile - a clean miss - produces a complete one.

Sources below `CodeCache::MIN_CACHEABLE_BYTES` skip the cache: serializing and storing them costs more than the
compile it would save.

## Keying

A blob's key is a 64-bit FNV-1a over:

- the **post-transform source** (after the BOM strip, the `js_strict` prelude and the `const ... = new Runnable`
  rewrite - see `docs/compatibility.md`) and its length;
- the **origin name**, which is baked into the compiled script (stack traces, error positions) and so is part of the
  blob's identity. It is the only `ScriptOrigin` field callers vary; if another starts varying, it has to be folded
  into the key too;
- the **build tag** - `ScriptCompiler::CachedDataVersionTag()`, V8's own version tag for cached data under the current
  V8 version and flags. V8's tag rather than a hash of `EngineFlags`, because the effective flag set is not the
  configured one (the engine appends `--expose-gc`, and `--single-threaded` under `EngineSingleThreaded`), and V8
  derives further flags by implication.

Because the key covers the exact bytes handed to V8, an edited script or a flipped compatibility flag is a natural
miss. Folding the build tag in means an entry from another V8 version or flag set is never *named*, rather than read
and then rejected. That matters for a shared directory: a rejected entry is dropped (unlinked), so two instances that
agreed on keys while disagreeing on flags would delete each other's entries on every compile.

A 64-bit key collision is the one way this could serve wrong code, and V8 is **not** a backstop for it: V8's source
check hashes the source *length* and the origin options, not the characters. The key mixes in the length precisely so
a collision needs matching lengths too. Do not weaken it.

## Disk layout

`CodeCachePath` names a directory of `<key>.cache` files, each **the V8 blob verbatim** - no framing of ours. V8's
cached data already carries its own magic, version, flag hash, source hash and checksum, and every mismatch it can
detect surfaces as `rejected` on the consuming compile.

Writes are staged to `<key>.<pid>.tmp` in the same directory and renamed into place, so a reader in another instance
sees either the previous entry or the new one, never a half-written file; the pid keeps two instances racing on the
same key off each other's staging file.

The directory is pruned at startup and again every ~32 MB written. A prune reaps stale `.tmp` staging files from a
killed instance, then, if the total exceeds `CodeCacheDiskLimit`, deletes oldest-written first until it fits. Entries
whose key no longer matches anything - an edited script, or a whole generation stranded by a V8 upgrade changing the
build tag - are never named again and are reclaimed here. A hot entry (written once, at its first miss) can be
deleted ahead of a stale one written more recently; the next compile of that source rewrites it, so the cost is one
recompile, never a wrong result.

Nothing about the disk tier is load-bearing. Every failure path - unwritable directory, lost rename race, corrupt
file - degrades to a compile.

## Settings

INI `[settings]`:

| Key | Default | Meaning |
|-----|---------|---------|
| `CodeCachePath` | (empty) | Directory for the disk tier; joined to the install dir when relative, replaces it when absolute. Empty leaves the disk tier off. Point several instances at one directory to share compiles. |
| `CodeCacheMemoryLimit` | 64 | In-memory budget, MB, clamped to [0, 1024]. 0 turns the memory tier off (and, with no disk path, skips serialization entirely rather than serializing and discarding). |
| `CodeCacheDiskLimit` | 256 | On-disk budget, MB. 0 disables the disk tier. |

The process is 32-bit, so the memory budget competes for address space with the script isolates (`MemoryLimit`
each) - raise it with that in mind.
