#include "hooks/StormSoundFixes.h"
#include <dsound.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include "hooks/InlinePatch.h"
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
bool installed = false;
std::array<uint8_t, 17> original{};

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

// A CALL replaces the 17-byte update block. ESI holds the request; EAX/ECX/EDX are
// overwritten by the continuation. Skip the 12 NOP bytes after the five-byte CALL.
__declspec(naked) void UpdateThunk() {
    __asm {
        mov ecx, esi
        call Update
        add dword ptr [esp], 12
        ret
    }
}
constexpr std::array<uint8_t, 17> EXPECTED = {0x83, 0x7E, 0x28, 0x00, 0x74, 0x38, 0x83, 0x7E, 0x3C,
                                              0x00, 0x74, 0x0D, 0xE8, 0x8A, 0xB0, 0xFF, 0xFF};
}  // namespace

void Install() {
    if (installed) {
        return;
    }
    const auto base = imports::GameModuleBase();
    const auto* site = reinterpret_cast<const uint8_t*>(base + 0x1A725);
    if (!std::ranges::equal(EXPECTED, std::span{site, EXPECTED.size()})) {
        utils::GetLogger("hooks.storm_sound")->error("Storm sound block differs from 1.14d; patch skipped");
        return;
    }
    refill = base + 0x157C0;
    WriteCallN(base + 0x1A725, reinterpret_cast<uintptr_t>(&UpdateThunk), EXPECTED.size(), original.data());
    installed = true;
}
void Remove() {
    if (installed) {
        RestoreN(imports::GameModuleBase() + 0x1A725, original.data(), original.size());
        installed = false;
    }
}
}  // namespace d2bs::lod114d::hooks::storm_sound
