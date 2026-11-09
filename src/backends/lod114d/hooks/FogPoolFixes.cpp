#include "hooks/FogPoolFixes.h"
#include <cstddef>
#include "detour/Hook.h"
#include "imports/ImportTypes.h"
#include "utils/CriticalSection.h"
#include "utils/utils.h"

namespace d2bs::lod114d::hooks::fog_pools {
namespace {
struct PoolSystemPrefix {
    uint32_t flags;
    CRITICAL_SECTION lock;
};
static_assert(offsetof(PoolSystemPrefix, lock) == 4);

void* __fastcall AllocateLocked(PoolSystemPrefix* system, void*, void* pool);
void* __fastcall FindLocked(PoolSystemPrefix* system, void*, const void* memory);

// Both original helpers take the system in EAX, one stack argument, and RET 4.
// Tail-adapt EAX to ECX; the unused EDX argument preserves the stack layout.
__declspec(naked) void* __stdcall AllocateThunk(void*) {
    __asm {
        mov ecx, eax
        jmp AllocateLocked
    }
}
__declspec(naked) void* __stdcall FindThunk(const void*) {
    __asm {
        mov ecx, eax
        jmp FindLocked
    }
}

core::detour::Hook<decltype(&AllocateThunk)> allocateHook{&AllocateThunk};
core::detour::Hook<decltype(&FindThunk)> findHook{&FindThunk};

void* __fastcall AllocateLocked(PoolSystemPrefix* system, void*, void* pool) {
    const utils::CriticalSectionLock guard(system->lock);
    const auto original = allocateHook.Real();
    void* result;
    __asm {
        mov eax, system
        push pool
        call original
        mov result, eax
    }
    return result;
}

void* __fastcall FindLocked(PoolSystemPrefix* system, void*, const void* memory) {
    const utils::CriticalSectionLock guard(system->lock);
    const auto original = findHook.Real();
    void* result;
    __asm {
        mov eax, system
        push memory
        call original
        mov result, eax
    }
    return result;
}
}  // namespace

void Install() {
    const auto base = imports::GameModuleBase();
    allocateHook.SetTarget(reinterpret_cast<decltype(&AllocateThunk)>(base + 0x95E0));
    findHook.SetTarget(reinterpret_cast<decltype(&FindThunk)>(base + 0x99D0));
    if (const auto error = core::detour::AttachAll({&allocateHook, &findHook}); error != 0) {
        utils::GetLogger("hooks.fog_pools")->error("Fog pool detours failed: {}", error);
    }
}
void Remove() {
    if (const auto error = core::detour::DetachAll({&allocateHook, &findHook}); error != 0) {
        utils::GetLogger("hooks.fog_pools")->error("Fog pool detach failed: {}", error);
    }
}
}  // namespace d2bs::lod114d::hooks::fog_pools
