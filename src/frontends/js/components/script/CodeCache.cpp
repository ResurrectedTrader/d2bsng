#include "CodeCache.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

#include "components/v8/V8Host.h"
#include "config/AppConfig.h"
#include "utils/utils.h"

namespace d2bs::js::script {

namespace {

constexpr uint64_t FNV64_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV64_PRIME = 1099511628211ULL;

uint64_t Fnv1a64(std::string_view text, uint64_t seed = FNV64_OFFSET) {
    uint64_t hash = seed;
    for (const char ch : text) {
        hash ^= static_cast<uint8_t>(ch);
        hash *= FNV64_PRIME;
    }
    return hash;
}

// One script's blob runs to a few hundred KB at most. Anything past this is a
// corrupt file we refuse to allocate for.
constexpr size_t MAX_ENTRY_BYTES = 64ULL * 1024 * 1024;

// Staging files this old belong to an instance that died mid-write.
constexpr auto TEMP_REAP_AGE = std::chrono::hours(1);

// Bytes written between disk prunes. Small enough to keep a long session near
// its budget, large enough that the directory walk is not on the hot path.
constexpr uint64_t PRUNE_AFTER_BYTES = 32ULL * 1024 * 1024;

constexpr size_t BYTES_PER_MB = 1024 * 1024;

// CachedDataVersionTag covers the V8 version and the *effective* flag set -
// including flags V8 derives by implication, which a hash of our own V8Flags
// string would miss (V8Host appends --expose-gc and, conditionally,
// --single-threaded). Reading it requires V8's flags to already be applied, so
// depend on V8Host explicitly rather than on the caller having gone first.
uint64_t CurrentBuildTag() {
    (void)V8Host::GetPlatform();
    return v8::ScriptCompiler::CachedDataVersionTag();
}

}  // namespace

CodeCache& CodeCache::Instance() {
    static CodeCache instance;
    return instance;
}

CodeCache::CodeCache() : buildTag_(CurrentBuildTag()), logger_(utils::GetLogger("CodeCache")) {
    const auto& cfg = config::GetAppConfig();
    memoryLimit_ = cfg.codeCacheMemoryLimit;
    diskLimit_ = cfg.codeCacheDiskLimit;

    if (cfg.codeCachePath.empty() || diskLimit_ == 0) {
        logger_->debug("memory tier only, limit {} MB", memoryLimit_ / BYTES_PER_MB);
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(cfg.codeCachePath, ec);
    if (!std::filesystem::is_directory(cfg.codeCachePath)) {
        logger_->warn("disk tier disabled - cannot create {}: {}", cfg.codeCachePath.string(), ec.message());
        return;
    }
    diskDir_ = cfg.codeCachePath;
    logger_->info("memory limit {} MB, disk limit {} MB at {}", memoryLimit_ / BYTES_PER_MB, diskLimit_ / BYTES_PER_MB,
                  diskDir_.string());
    PruneDisk();
}

uint64_t CodeCache::MakeKey(std::string_view originName, std::string_view source) const {
    // The origin is baked into the compiled script (stack traces, error
    // positions), so it is part of the blob's identity even though the source
    // bytes dominate it. It is currently the only ScriptOrigin field callers
    // vary - should another start varying (a line/column offset, module flags),
    // it has to be folded in here too. The source length goes in as well, so
    // sources differing only by a trailing run can't reach the same hash state.
    uint64_t hash = Fnv1a64(originName, buildTag_);
    hash = (hash ^ source.size()) * FNV64_PRIME;
    return Fnv1a64(source, hash);
}

CodeCache::Blob CodeCache::Lookup(uint64_t key) {
    {
        std::scoped_lock lock(mutex_);
        if (auto it = index_.find(key); it != index_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second);
            return it->second->blob;
        }
    }
    // Disk I/O stays off the lock - a miss here is the slow path already.
    auto blob = ReadDisk(key);
    if (blob) {
        // Promotion must not clobber: another thread can have stored a blob for
        // this key while we were in the read, and that one is at least as good
        // as the on-disk generation we just loaded.
        Insert(key, blob, /*onlyIfAbsent=*/true);
    }
    return blob;
}

void CodeCache::Store(uint64_t key, v8::Local<v8::UnboundScript> script) {
    if (memoryLimit_ == 0 && diskDir_.empty()) {
        // Both tiers off - serializing would be pure cost on every compile.
        return;
    }
    const std::unique_ptr<v8::ScriptCompiler::CachedData> data(v8::ScriptCompiler::CreateCodeCache(script));
    if (!data || data->data == nullptr || data->length <= 0 || static_cast<size_t>(data->length) > MAX_ENTRY_BYTES) {
        return;
    }
    auto blob = std::make_shared<const std::vector<uint8_t>>(data->data, data->data + data->length);
    Insert(key, blob);
    WriteDisk(key, *blob);
}

void CodeCache::Drop(uint64_t key) {
    {
        std::scoped_lock lock(mutex_);
        if (auto it = index_.find(key); it != index_.end()) {
            memoryBytes_ -= it->second->blob->size();
            lru_.erase(it->second);
            index_.erase(it);
        }
    }
    EraseDisk(key);
}

void CodeCache::Insert(uint64_t hash, Blob blob, bool onlyIfAbsent) {
    if (memoryLimit_ == 0) {
        return;
    }
    std::scoped_lock lock(mutex_);
    if (auto it = index_.find(hash); it != index_.end()) {
        if (onlyIfAbsent) {
            return;
        }
        memoryBytes_ -= it->second->blob->size();
        lru_.erase(it->second);
        index_.erase(it);
    }
    memoryBytes_ += blob->size();
    lru_.push_front({.hash = hash, .blob = std::move(blob)});
    index_[hash] = lru_.begin();
    EvictLocked();
}

void CodeCache::EvictLocked() {
    // A blob larger than the whole budget evicts itself on the way in; the
    // caller still holds its shared_ptr, so the compile in flight is unaffected.
    while (memoryBytes_ > memoryLimit_ && !lru_.empty()) {
        const auto& victim = lru_.back();
        memoryBytes_ -= victim.blob->size();
        index_.erase(victim.hash);
        lru_.pop_back();
    }
}

std::filesystem::path CodeCache::DiskPath(uint64_t hash) const {
    return diskDir_ / std::format("{:016x}.cache", hash);
}

// A disk entry is the blob verbatim - no framing of ours. V8's cached data
// carries its own magic, version, flag hash and checksum, and a mismatch on any
// of them surfaces as `rejected` on the consuming compile, so a wrapper
// repeating those would be dead weight.
//
// What V8 does NOT check is the source text: its source hash is built from the
// source *length* and the origin options, not the characters. So the key is the
// only thing standing between two same-length sources that hash alike and one
// executing the other's bytecode. MakeKey folds in the length precisely so a
// collision needs matching lengths as well; at 64 bits that is not a risk worth
// re-adding a header for, but it is the reason the key must not be weakened.
CodeCache::Blob CodeCache::ReadDisk(uint64_t hash) const {
    if (diskDir_.empty()) {
        return nullptr;
    }
    const auto path = DiskPath(hash);

    // Size-check before reading rather than after: this is a 32-bit process, so
    // a corrupt file must not get to size the allocation.
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize == 0 || fileSize > MAX_ENTRY_BYTES) {
        return nullptr;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return nullptr;
    }
    std::string bytes((std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return nullptr;
    }
    return std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end());
}

void CodeCache::WriteDisk(uint64_t hash, const std::vector<uint8_t>& blob) const {
    if (diskDir_.empty()) {
        return;
    }
    // Stage next to the target (same volume, so the swap is a rename) and move
    // it into place: a reader in another instance sees either the previous entry
    // or the new one, never a half-written file. The pid keeps two instances
    // racing on the same key off each other's staging file.
    const auto temp = diskDir_ / std::format("{:016x}.{}.tmp", hash, GetCurrentProcessId());
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            logger_->debug("cannot stage {}", temp.string());
            return;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - byte buffer through a char stream
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        out.close();
        if (!out) {
            std::error_code writeEc;
            std::filesystem::remove(temp, writeEc);
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, DiskPath(hash), ec);
    if (ec) {
        // Losing the race with another instance's rename, or with a reader
        // holding the target open, costs nothing: the entry is still in memory
        // and the next miss retries.
        logger_->debug("cannot commit {}: {}", DiskPath(hash).string(), ec.message());
        std::filesystem::remove(temp, ec);
        return;
    }

    // Re-prune periodically rather than only at startup: a long session that
    // keeps compiling new sources (every script edit mints a new key) would
    // otherwise grow the directory past the budget until the next launch. The
    // exchange leaves exactly one thread pruning per interval.
    if (bytesSincePrune_.fetch_add(blob.size(), std::memory_order_relaxed) + blob.size() >= PRUNE_AFTER_BYTES) {
        bytesSincePrune_.store(0, std::memory_order_relaxed);
        PruneDisk();
    }
}

void CodeCache::EraseDisk(uint64_t hash) const {
    if (diskDir_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(DiskPath(hash), ec);
}

// Entries whose key no longer matches anything - a script that changed, or a
// whole generation stranded by a V8 upgrade changing the build tag - are never
// named again and are reclaimed here.
//
// Eviction is by write time, not use: a read never touches the file, and
// touching it on every hit would trade the I/O this cache exists to avoid for a
// better ordering. That means a hot entry (written once, at its first miss) can
// be evicted ahead of a stale one written more recently. It self-corrects - the
// next compile of that source rewrites it as the newest entry - so the cost is
// one recompile, not a wrong result.
void CodeCache::PruneDisk() const {
    struct Existing {
        std::filesystem::path path;
        uintmax_t size = 0;
        std::filesystem::file_time_type written;
    };

    std::vector<Existing> entries;
    uintmax_t total = 0;
    const auto now = std::filesystem::file_time_type::clock::now();

    std::error_code iterEc;
    for (const auto& entry : std::filesystem::directory_iterator(diskDir_, iterEc)) {
        std::error_code ec;
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const auto written = entry.last_write_time(ec);
        if (ec) {
            continue;
        }
        if (entry.path().extension() == ".tmp") {
            if (now - written > TEMP_REAP_AGE) {
                std::filesystem::remove(entry.path(), ec);
            }
            continue;
        }
        if (entry.path().extension() != ".cache") {
            continue;
        }
        const auto size = entry.file_size(ec);
        if (ec) {
            continue;
        }
        total += size;
        entries.push_back({.path = entry.path(), .size = size, .written = written});
    }

    if (total <= diskLimit_) {
        return;
    }
    std::ranges::sort(entries, {}, &Existing::written);
    for (const auto& entry : entries) {
        if (total <= diskLimit_) {
            break;
        }
        std::error_code ec;
        if (std::filesystem::remove(entry.path, ec)) {
            total -= entry.size;
        }
    }
    logger_->debug("pruned disk tier to {} MB", total / BYTES_PER_MB);
}

}  // namespace d2bs::js::script
