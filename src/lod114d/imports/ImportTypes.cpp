#include "imports/ImportTypes.h"

#ifdef _DEBUG
    #include <Windows.h>
    #include <cstdio>
#endif

namespace d2bs::imports {

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
    std::snprintf(buf, sizeof(buf), "[d2bs::imports] ResolveAll resolved %zu imports against base 0x%08zX\n",
                  items_.size(), static_cast<size_t>(base));
    OutputDebugStringA(buf);
#endif
}

}  // namespace d2bs::imports
