#include "hooks/InlinePatch.h"

#include <Windows.h>

#include <cstring>

namespace d2bs::hooks {

namespace {

constexpr uint8_t OPCODE_JMP_REL32 = 0xE9;
constexpr uint8_t OPCODE_CALL_REL32 = 0xE8;
constexpr uint8_t OPCODE_NOP = 0x90;

void WriteRel(uintptr_t site, uint8_t opcode, uintptr_t target, size_t len, uint8_t* originalOut) {
    auto* p = reinterpret_cast<uint8_t*>(site);
    DWORD oldProtect = 0;
    VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(originalOut, p, len);
    p[0] = opcode;
    const int32_t rel = static_cast<int32_t>(target - (site + 5));
    std::memcpy(p + 1, &rel, sizeof(int32_t));
    if (len > 5) {
        std::memset(p + 5, OPCODE_NOP, len - 5);
    }
    VirtualProtect(p, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, len);
}

}  // namespace

void WriteCallN(uintptr_t site, uintptr_t target, size_t len, uint8_t* originalOut) {
    WriteRel(site, OPCODE_CALL_REL32, target, len, originalOut);
}

void WriteJmpN(uintptr_t site, uintptr_t target, size_t len, uint8_t* originalOut) {
    WriteRel(site, OPCODE_JMP_REL32, target, len, originalOut);
}

void RestoreN(uintptr_t site, const uint8_t* original, size_t len) {
    auto* p = reinterpret_cast<uint8_t*>(site);
    DWORD oldProtect = 0;
    VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(p, original, len);
    VirtualProtect(p, len, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, len);
}

void WriteByte(uintptr_t site, uint8_t value, uint8_t& originalOut) {
    auto* p = reinterpret_cast<uint8_t*>(site);
    DWORD oldProtect = 0;
    VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
    originalOut = *p;
    *p = value;
    VirtualProtect(p, 1, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
}

void RestoreByte(uintptr_t site, uint8_t value) {
    auto* p = reinterpret_cast<uint8_t*>(site);
    DWORD oldProtect = 0;
    VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
    *p = value;
    VirtualProtect(p, 1, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
}

}  // namespace d2bs::hooks
