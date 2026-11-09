#include "hooks/StormSoundFixes.h"
#include <dsound.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include "detour/Hook.h"
#include "imports/ImportTypes.h"
#include "utils/CriticalSection.h"
#include "utils/utils.h"

namespace d2bs::lod114d::hooks::storm_sound {
namespace {
struct Channel {
    std::array<uint32_t, 6> reserved;
    uint32_t state;
};
struct Request {
    std::array<uint32_t, 10> reserved;
    IDirectSoundBuffer* buffer;
    uint32_t reserved2;
    Channel* channel;
    uint32_t readSize;
    uint32_t reserved3;
    uint32_t bytesRead;
};
static_assert(offsetof(Request, buffer) == 0x28 && offsetof(Request, channel) == 0x30 &&
              offsetof(Request, bytesRead) == 0x3C && offsetof(Channel, state) == 0x18);
imports::GameVar<CRITICAL_SECTION> readLock{0x379068};
uintptr_t refill = 0;
uintptr_t continuation = 0;

void Refill(Request* request) {
    // SFILE_CommitAudioRequest takes its argument in ESI, preserved by the compiler.
    __asm {
        mov esi, request
        call dword ptr [refill]
    }
}

void __fastcall Update(Request* request) {
    const utils::CriticalSectionLock guard(*readLock);
    if (request->buffer == nullptr) {
        return;
    }
    if (request->bytesRead != 0) {
        // Refill recursively takes readLock; SFileDdaEnd cannot clear/free the buffer between check and use.
        Refill(request);
    } else if (request->readSize != 0) {
        request->buffer->Stop();
        if (request->channel != nullptr) {
            request->channel->state = 4;
        }
    }
}

// This is a basic-block detour, not a callable function entry. The original worker's ESI holds
// the request; its next block reloads ESI and overwrites EAX/ECX/EDX before using them.
// Returning would pop the worker's caller, so jump to the existing common continuation instead.
__declspec(naked) void UpdateThunk() {
    __asm {
        mov ecx, esi
        call Update
        jmp dword ptr [continuation]
    }
}
core::detour::Hook<decltype(&UpdateThunk)> updateHook{&UpdateThunk};
constexpr std::array<uint8_t, 17> EXPECTED = {0x83, 0x7E, 0x28, 0x00, 0x74, 0x38, 0x83, 0x7E, 0x3C,
                                              0x00, 0x74, 0x0D, 0xE8, 0x8A, 0xB0, 0xFF, 0xFF};
}  // namespace

void Install() {
    if (updateHook.IsAttached()) {
        return;
    }
    const auto base = imports::GameModuleBase();
    const auto* site = reinterpret_cast<const uint8_t*>(base + 0x1A725);
    if (!std::ranges::equal(EXPECTED, std::span{site, EXPECTED.size()})) {
        utils::GetLogger("hooks.storm_sound")->error("Storm sound block differs from 1.14d; detour skipped");
        return;
    }
    refill = base + 0x157C0;
    continuation = base + 0x1A736;
    updateHook.SetTarget(reinterpret_cast<decltype(&UpdateThunk)>(base + 0x1A725));
    if (const auto error = core::detour::AttachAll({&updateHook}); error != 0) {
        utils::GetLogger("hooks.storm_sound")->error("Storm sound detour failed: {}", error);
    }
}
void Remove() {
    if (const auto error = core::detour::DetachAll({&updateHook}); error != 0) {
        utils::GetLogger("hooks.storm_sound")->error("Storm sound detach failed: {}", error);
    }
}
}  // namespace d2bs::lod114d::hooks::storm_sound
