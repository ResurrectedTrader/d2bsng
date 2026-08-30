#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>
#include <v8.h>

namespace d2bs::js::script {

// Process-wide cache of V8 code-cache blobs (the serialized result of parsing
// and compiling a source), keyed by the source a compile was handed.
//
// Every script runs in its own isolate, so V8's per-isolate compilation cache
// buys nothing across scripts: a bot that spawns half a dozen script threads per
// game re-parses and re-compiles the same libraries once per isolate, every
// game. Code-cache blobs are isolate-independent, which is exactly the axis that
// repetition lives on.
//
// Two tiers. The in-memory tier is always on and covers every isolate after the
// first in this process. The on-disk tier is opt-in (AppConfig::codeCachePath)
// and additionally covers the first compile after launch plus other game
// instances pointed at the same directory. Both are byte-capped; memory evicts
// least-recently-used, disk least-recently-written (see PruneDisk).
//
// The cache holds byte vectors, never V8 handles, so its lifetime carries no
// ordering dependency on V8Host teardown.
class CodeCache {
   public:
    // Handed out as a shared_ptr because a compile keeps the bytes alive for the
    // whole ScriptCompiler::Source lifetime while another script thread may
    // evict the entry from under it.
    using Blob = std::shared_ptr<const std::vector<uint8_t>>;

    static CodeCache& Instance();

    // Below this, serializing and storing a blob (and with the disk tier,
    // writing it out) costs more than the compile it would save.
    static constexpr size_t MIN_CACHEABLE_BYTES = 1024;
    static bool IsCacheable(size_t sourceSize) { return sourceSize >= MIN_CACHEABLE_BYTES; }

    // Identity of one compile: the post-transform source, the origin baked into
    // the compiled script, and the V8 build the blob would be deserialized by.
    // Covering the build here rather than validating it on read means an entry
    // from another V8 version or flag set is never named, so instances that
    // disagree simply use disjoint keys.
    uint64_t MakeKey(std::string_view originName, std::string_view source) const;

    // Null when neither tier holds `key`. A disk hit is promoted into memory.
    Blob Lookup(uint64_t key);

    // Serialize `script` and store it under `key`. Does nothing if V8 declines
    // to serialize it or the blob is implausibly large.
    void Store(uint64_t key, v8::Local<v8::UnboundScript> script);

    // Forget `key` in both tiers.
    void Drop(uint64_t key);

    CodeCache(const CodeCache&) = delete;
    CodeCache& operator=(const CodeCache&) = delete;

   private:
    CodeCache();
    ~CodeCache() = default;

    struct Entry {
        uint64_t hash = 0;
        Blob blob;
    };

    // onlyIfAbsent leaves an entry another thread published in the meantime
    // alone - used by the disk-promotion path, whose blob is never fresher.
    void Insert(uint64_t hash, Blob blob, bool onlyIfAbsent = false);
    void EvictLocked();

    std::filesystem::path DiskPath(uint64_t hash) const;
    Blob ReadDisk(uint64_t hash) const;
    void WriteDisk(uint64_t hash, const std::vector<uint8_t>& blob) const;
    void EraseDisk(uint64_t hash) const;
    void PruneDisk() const;

    // MRU at the front. Guarded by mutex_ along with index_ and memoryBytes_.
    std::list<Entry> lru_;
    std::unordered_map<uint64_t, std::list<Entry>::iterator> index_;
    size_t memoryBytes_ = 0;
    std::mutex mutex_;

    size_t memoryLimit_ = 0;
    // 64-bit: a disk budget is not bounded by this 32-bit process's address space.
    uint64_t diskLimit_ = 0;
    mutable std::atomic<uint64_t> bytesSincePrune_{0};
    // Empty when the disk tier is off - either unconfigured or the directory
    // could not be created.
    std::filesystem::path diskDir_;
    // V8's own CachedData version tag (version + effective flags), folded into
    // every key.
    uint64_t buildTag_ = 0;

    std::shared_ptr<spdlog::logger> logger_;
};

}  // namespace d2bs::js::script
