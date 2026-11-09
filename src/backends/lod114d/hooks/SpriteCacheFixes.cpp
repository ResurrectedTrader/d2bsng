#include "hooks/SpriteCacheFixes.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include "detour/Hook.h"
#include "hooks/InlinePatch.h"
#include "imports/ImportTypes.h"
#include "utils/CriticalSection.h"
#include "utils/utils.h"

namespace d2bs::lod114d::hooks::sprite_cache {
namespace {
struct Entry {
    void* buffer;
    uint32_t size;
    void* payload;
    uint32_t flags;
    uint32_t touched;
    Entry* next;
    Entry* previous;
};
struct Cache {
    void* pool;
    uint32_t budget, bytes, count;
    Entry* entries;
    Entry* touchedHead;
    Entry* touchedTail;
    Entry* untouchedHead;
    Entry* untouchedTail;
    Entry* freeHead;
    Entry* freeTail;
    void(__fastcall* release)(void*);
    CRITICAL_SECTION lock;
};
static_assert(sizeof(Entry) == 0x1C && offsetof(Cache, lock) == 0x30);
imports::GameVar<Cache> cache{0x49DB60};
imports::FastcallFunc<int(void*, void*, const char*, uint32_t, uint32_t)> freeMemory{0xB480};
bool installed = false;
uintptr_t pinContinuation = 0;

// Caller owns the cache lock. Unlike the native unlink helper, moving a pinned head is legal:
// pinning prevents eviction, not migration from the untouched list to the touched list.
void Unlink(Cache* owner, Entry* entry) {
    auto*& head = (entry->flags & 1U) != 0 ? owner->untouchedHead : owner->touchedHead;
    auto*& tail = (entry->flags & 1U) != 0 ? owner->untouchedTail : owner->touchedTail;
    if (entry->previous != nullptr) {
        entry->previous->next = entry->next;
    } else {
        head = entry->next;
    }
    if (entry->next != nullptr) {
        entry->next->previous = entry->previous;
    } else {
        tail = entry->previous;
    }
    entry->next = nullptr;
    entry->previous = nullptr;
}

Entry* Evict(Cache* owner, Entry* entry, uint32_t untouchedFirst, Entry* except, bool keepLastUsed) {
    {
        const utils::CriticalSectionLock guard(owner->lock);
        if (owner->count == 0) {
            return nullptr;
        }
        if (entry == nullptr) {
            bool untouched = false;
            if (owner->untouchedHead != nullptr) {
                untouched = owner->touchedHead == nullptr
                                ? untouchedFirst != 0
                                : untouchedFirst != 0 && owner->untouchedHead->touched < owner->touchedHead->touched;
            }
            entry = untouched ? owner->untouchedHead : owner->touchedHead;
        }
        if (entry == nullptr || (entry->flags & 2U) != 0 || entry == except ||
            (keepLastUsed && entry == owner->touchedTail)) {
            return nullptr;
        }
        Unlink(owner, entry);
    }
    // Release can wait for the read worker, which needs the cache lock to pin/charge its decode.
    if (entry->payload != nullptr) {
        owner->release(entry->payload);
        entry->payload = nullptr;
    }
    if (entry->buffer != nullptr) {
        freeMemory(owner->pool, entry->buffer, "d2bsng", __LINE__, 0);
        entry->buffer = nullptr;
    }
    {
        const utils::CriticalSectionLock guard(owner->lock);
        // A queued read may have increased size while release waited. Subtract its final size once.
        owner->bytes -= entry->size;
        entry->previous = owner->freeTail;
        entry->next = nullptr;
        if (owner->freeTail != nullptr) {
            owner->freeTail->next = entry;
        } else {
            owner->freeHead = entry;
        }
        owner->freeTail = entry;
        --owner->count;
    }
    return entry;
}

Entry* __fastcall EvictHook(Cache* owner, Entry* entry, uint32_t untouchedFirst) {
    return Evict(owner, entry, untouchedFirst, nullptr, false);
}

// Native GrowBlock has precisely two call sites: read completion passes 0, issue passes 1.
// A read completion must not evict: it can otherwise release a sprite while the game uses it.
void __fastcall GrowHook(Cache* owner, Entry* entry, uint32_t size, uint32_t untouchedFirst) {
    {
        const utils::CriticalSectionLock guard(owner->lock);
        entry->size += size;
        owner->bytes += size;
        if (untouchedFirst == 0) {
            entry->flags &= ~2U;
            return;
        }
    }
    for (;;) {
        {
            const utils::CriticalSectionLock guard(owner->lock);
            if (owner->bytes <= owner->budget) {
                return;
            }
        }
        // The read may finish during trimming, so exclude the issuing entry by identity rather
        // than borrowing its decode pin. Retain the last sprite handed to the current draw too.
        if (Evict(owner, nullptr, untouchedFirst, entry, true) == nullptr) {
            return;
        }
    }
}

void __fastcall Pin(Entry* entry) {
    const utils::CriticalSectionLock guard(cache->lock);
    entry->flags |= 2U;
}
__declspec(naked) void PinThunk() {
    __asm {
        mov edi, [ebx+0x94]
        push eax
        mov ecx, edi
        call Pin
        pop eax
        jmp dword ptr [pinContinuation]
    }
}
core::detour::Hook<decltype(&EvictHook)> evictHook{&EvictHook};
core::detour::Hook<decltype(&GrowHook)> growHook{&GrowHook};
core::detour::Hook<decltype(&PinThunk)> pinHook{&PinThunk};

constexpr std::array<uint8_t, 10> PIN = {0x8B, 0xBB, 0x94, 0, 0, 0, 0x83, 0x4F, 0x0C, 0x02};
constexpr std::array<uint8_t, 4> CLEAR = {0x83, 0x67, 0x0C, 0xFD};
constexpr std::array<uint8_t, 10> UNTOUCHED_PIN = {0xF6, 0x42, 0x0C, 0x02, 0x0F, 0x85, 0xA2, 0, 0, 0};
constexpr std::array<uint8_t, 6> TOUCHED_PIN = {0xF6, 0x42, 0x0C, 0x02, 0x75, 0x59};

template <size_t N>
bool Matches(uint32_t rva, const std::array<uint8_t, N>& bytes) {
    const auto address = imports::GameModuleBase() + rva;
    if (!std::ranges::equal(bytes, std::span{reinterpret_cast<const uint8_t*>(address), N})) {
        utils::GetLogger("hooks.sprite_cache")
            ->warn("Sprite cache bytes at {:#x} differ from 1.14d; fixes skipped", address);
        return false;
    }
    return true;
}
template <size_t N>
void Nop(uint32_t rva, const std::array<uint8_t, N>&) {
    std::array<uint8_t, N> nops{}, original{};
    nops.fill(0x90);
    WriteN(imports::GameModuleBase() + rva, nops.data(), N, original.data());
}
}  // namespace

void Install() {
    if (installed) {
        return;
    }
    constexpr std::array<uint8_t, 8> EVICT_ENTRY = {0x55, 0x8B, 0xEC, 0x53, 0x56, 0x8B, 0xF1, 0x33};
    constexpr std::array<uint8_t, 8> GROW_ENTRY = {0x55, 0x8B, 0xEC, 0x53, 0x56, 0x57, 0x8B, 0xF1};
    if (!Matches(0x1FF4A0, EVICT_ENTRY) || !Matches(0x1FF630, GROW_ENTRY) || !Matches(0x1FF6EF, PIN) ||
        !Matches(0x1FF726, CLEAR) || !Matches(0x1FF1EB, UNTOUCHED_PIN) || !Matches(0x1FF238, TOUCHED_PIN)) {
        return;
    }
    const auto base = imports::GameModuleBase();
    pinContinuation = base + 0x1FF6F9;
    evictHook.SetTarget(reinterpret_cast<decltype(&EvictHook)>(base + 0x1FF4A0));
    growHook.SetTarget(reinterpret_cast<decltype(&GrowHook)>(base + 0x1FF630));
    pinHook.SetTarget(reinterpret_cast<decltype(&PinThunk)>(base + 0x1FF6EF));
    if (const auto error = core::detour::AttachAll({&evictHook, &growHook, &pinHook}); error != 0) {
        utils::GetLogger("hooks.sprite_cache")->error("Sprite cache detours failed: {}", error);
        return;
    }
    // Native unlink: remove only the pinned-head refusal; touching still moves the entry normally.
    Nop(0x1FF1EB, UNTOUCHED_PIN);
    Nop(0x1FF238, TOUCHED_PIN);
    // Decode pin now clears with the read's size charge under the lock in GrowHook.
    Nop(0x1FF726, CLEAR);
    installed = true;
}
void Remove() {
    if (!installed) {
        return;
    }
    if (const auto error = core::detour::DetachAll({&evictHook, &growHook, &pinHook}); error != 0) {
        utils::GetLogger("hooks.sprite_cache")->error("Sprite cache detach failed: {}", error);
        return;
    }
    const auto base = imports::GameModuleBase();
    RestoreN(base + 0x1FF726, CLEAR.data(), CLEAR.size());
    RestoreN(base + 0x1FF238, TOUCHED_PIN.data(), TOUCHED_PIN.size());
    RestoreN(base + 0x1FF1EB, UNTOUCHED_PIN.data(), UNTOUCHED_PIN.size());
    installed = false;
}
}  // namespace d2bs::lod114d::hooks::sprite_cache
