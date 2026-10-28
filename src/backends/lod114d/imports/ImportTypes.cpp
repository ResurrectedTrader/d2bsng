#include "imports/ImportTypes.h"

#include <Windows.h>

#include <atomic>

#ifdef _DEBUG
    #include <cstdio>
#endif

namespace d2bs::lod114d::imports {

namespace {

std::atomic<uintptr_t> gameModuleBase{0};

}  // namespace

uintptr_t GameModuleBase() noexcept {
    const uintptr_t base = gameModuleBase.load(std::memory_order_acquire);
    return base != 0 ? base : reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

void SetGameModuleBase(uintptr_t base) noexcept {
    gameModuleBase.store(base, std::memory_order_release);
}

Registry& Registry::Get() noexcept {
    // Meyers singleton - constructed on first call, destroyed at process
    // exit. Thread-safe initialisation per C++11. Every GameFunc/GameVar/
    // GameAsmFunc constructor calls this from static-init phase, so the
    // singleton is fully populated before Bridge::Init() runs.
    static Registry instance;
    return instance;
}

// NOLINTNEXTLINE(bugprone-exception-escape) - push_back throw on OOM is intentional terminate; comment below
void Registry::Register(IImport* item) noexcept {
    // Self-registration from constructors. items_.push_back can theoretically
    // throw on allocation failure; Win32 process is doomed in that case so
    // letting std::terminate fire from noexcept is acceptable.
    items_.push_back(item);
}

void Registry::ResolveAll(uintptr_t base) noexcept {
    for (IImport* item : items_) {
        item->Resolve(base);
    }
#ifdef _DEBUG
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[d2bs::lod114d::imports] ResolveAll resolved %zu imports against base 0x%08zX\n",
                  items_.size(), static_cast<size_t>(base));
    OutputDebugStringA(buf);
#endif
}

}  // namespace d2bs::lod114d::imports
